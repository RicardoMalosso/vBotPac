"""Direct PID attachment, serialized RPC and no-retry failure semantics."""
import json
import math
from pathlib import Path
import uuid
from controller_transport import ControllerTransport
from discover_client import find_clients, select_client


def lua_literal(value):
    if value is None:return 'nil'
    if isinstance(value,bool):return 'true' if value else 'false'
    if isinstance(value,(int,float)):
        if not math.isfinite(value):raise ValueError('Nonfinite RPC argument')
        return repr(value)
    if isinstance(value,str):
        return '"'+''.join('\\%03d'%b if b<32 or b>=127 else '\\'+chr(b) if b in (34,92) else chr(b) for b in value.encode('utf-8'))+'"'
    if isinstance(value,list):return '{'+','.join(lua_literal(x) for x in value)+'}'
    if isinstance(value,dict):return '{'+','.join('['+lua_literal(k)+']='+lua_literal(v) for k,v in value.items())+'}'
    raise ValueError('Unsupported RPC value')


class DirectBroker:
    def __init__(self, selected, profile, controller, dll, transport=None):
        # Discovery is revalidated after the picker/build, before injection.
        self.selected=select_client(find_clients(profile),selected['pid'],selected['target']) if transport is None else selected
        self.pid=self.selected['pid'];self.token=uuid.uuid4().hex;self.closed=False
        self.transport=transport or ControllerTransport(str(controller),self.pid,str(dll))
        try:
            source=Path(__file__).with_name('game_bridge.lua').read_text(encoding='utf-8')
            self.execute('return (function(...) '+source+' end)('+lua_literal(self.token)+')')
            self.read()
        except BaseException:
            self.transport.close();self.closed=True;raise

    def execute(self, source):
        if self.closed:raise ValueError('Attachment is closed; reconnect explicitly')
        try:
            response=self.transport.request('exec '+source)
        except BaseException:
            self.closed=True;self.transport.close();raise
        if not response.startswith('OK\n'):
            raise ValueError(response.removeprefix('ERROR\n'))
        return response[3:]

    def rpc(self, request):
        value=dict(request,token=self.token)
        return json.loads(self.execute('return assert(__vbotpac_bridge,"Attachment ended").dispatch('+lua_literal(value)+')'))

    def read(self):
        result=self.rpc({'op':'snapshot'})
        if not result['ok']:raise ValueError(result['error'])
        value=result.get('value') or {}
        if not value.get('online') or not value.get('position'):raise ValueError('Selected client has no online character')
        return dict(value,pid=self.pid)

    def close(self):
        if not self.closed:
            try:self.rpc({'op':'close'})
            finally:self.closed=True;self.transport.close()


def process_mailbox(broker, folder, previous):
    """One request once. Never retry a potentially mutating command."""
    import time
    request_path=folder/'request.json'
    if not request_path.exists():return previous
    try:request=json.loads(request_path.read_text(encoding='utf-8'))
    except (OSError, ValueError):return previous  # Atomic rename normally prevents partial reads.
    request_path.unlink()
    sequence=request.get('seq')
    if type(sequence) is not int or sequence<=previous:
        raise ValueError('Replayed/out-of-order UI request')
    if request.get('token')!=broker.token or request.get('expires',0)<time.time():
        result={'ok':False,'error':'Expired or mismatched attachment request'}
    else:
        request={k:v for k,v in request.items() if k not in ('seq','token','expires')}
        if request.get('op')=='call' and isinstance(request.get('args'),dict):
            request['args']={int(k):v for k,v in request['args'].items()}
        try:result=broker.rpc(request)
        except Exception:
            broker.closed=True;broker.transport.close()
            raise  # An uncertain call is never retried.
    result.update(seq=sequence,token=broker.token)
    temporary=folder/'response.tmp';temporary.write_text(json.dumps(result),encoding='utf-8');temporary.replace(folder/'response.json')
    return sequence
