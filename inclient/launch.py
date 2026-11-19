"""Select an open client and load the complete fork through the native DLL."""
import argparse
import hashlib
import json
from pathlib import Path
import queue
import sys
import threading
import time
import uuid
ROOT=Path(__file__).resolve().parents[1]
sys.path.insert(0,str(ROOT/'external'))
from controller_transport import ControllerTransport
from discover_client import find_clients, select_client
from native import binaries
from package import stage


def literal(value):
    if value is None:return 'nil'
    if isinstance(value,bool):return 'true' if value else 'false'
    if isinstance(value,int):return str(value)
    if isinstance(value,str):return '"'+''.join('\\%03d'%b if b<32 or b>=127 else '\\'+chr(b) if b in (34,92) else chr(b) for b in value.encode())+'"'
    if isinstance(value,list):return '{'+','.join(literal(x) for x in value)+'}'
    if isinstance(value,dict):return '{'+','.join('['+literal(k)+']='+literal(v) for k,v in value.items())+'}'
    raise ValueError('Unsupported launcher value')


def pick(discovery,pid=None):
    matches=discovery['matches']
    if pid is None and len(matches)>1:
        print('Open compatible clients:')
        for c in matches:print(str(c['pid'])+'  '+c['target'])
        while True:
            text=input('Client PID: ').strip()
            if text.isdecimal() and any(c['pid']==int(text) for c in matches):pid=int(text);break
            print('Choose one of the listed PIDs.')
    return select_client(discovery,pid)


class ModuleSession:
    def __init__(self,transport,token=None):self.transport=transport;self.token=token or uuid.uuid4().hex;self.bound=False;self.loaded=False
    def request(self,source):
        value=self.transport.request('module-v1 '+self.token+' '+source)
        if not value.startswith('OK\n'):raise ValueError(value.removeprefix('ERROR\n'))
        return value[3:]
    def bind(self):
        capability=self.transport.request('module-capabilities')
        if capability!='OK\nvBotPac module loader v2; private-files':
            raise ValueError('The loaded DLL lacks the in-client module loader. Restart the game once after building this branch; no fallback was used.')
        result=json.loads(self.transport.request('atomic-bind '+self.token))
        if result.get('status')!='bound':raise ValueError('Cannot bind selected game Lua state: '+str(result))
        self.bound=True
    def load(self,selected):
        write_dir=self.request("return assert(g_resources.getWriteDir(),'Client write directory unavailable')")
        character=self.request("local p=g_game.getLocalPlayer();assert(p and g_game.isOnline(),'Log into the selected character first');return p:getName()")
        data_root=ROOT/'data/inclient'
        spec=stage(data_root,selected,character,legacy_dir=write_dir);spec['token']=self.token
        settings_file=data_root/spec['settings'].lstrip('/')
        if not settings_file.exists():
            settings=json.loads(self.request("return json.encode(g_settings.getNode('vbotpac_inclient') or {})"))
            settings_file.parent.mkdir(parents=True,exist_ok=True)
            settings_file.write_text(json.dumps({'bot':settings}),encoding='utf-8')
        source='local SPEC='+literal(spec)+'\n'+(ROOT/'inclient/bootstrap.lua').read_text(encoding='utf-8')
        if len(source.encode())+43>65541:raise ValueError('Bootstrap exceeds the controller frame limit')
        self.trace=data_root/spec['trace'].lstrip('/')
        self.log=data_root/spec['log'].lstrip('/')
        result=self.request(source);self.loaded=True;return result
    def control(self,operation):return self.request('return assert(_G.__vbotpac_module,"Runtime ended").control('+literal(self.token)+','+literal(operation)+')')
    def close(self):
        try:
            if self.loaded:
                try:self.control('unload')
                except (OSError,ValueError,TimeoutError):pass
            if self.bound:
                try:self.transport.request('release '+self.token)
                except (OSError,ValueError,TimeoutError):pass
        finally:self.transport.close()


def main():
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--pid',type=int)
    parser.add_argument('--profile',type=Path,default=ROOT/'external/profiles/exoria74-gl.json')
    parser.add_argument('--controller',type=Path);parser.add_argument('--dll',type=Path)
    args=parser.parse_args()
    try:
        selected=pick(find_clients(args.profile),args.pid)
        controller,dll=binaries(args.controller,args.dll)
        selected=select_client(find_clients(args.profile),selected['pid'],selected['target'])
        session=ModuleSession(ControllerTransport(str(controller),selected['pid'],str(dll)))
    except (OSError,ValueError,TimeoutError) as error:
        print(str(error),file=sys.stderr);return 1
    try:
        session.bind();print('PID',selected['pid'],session.load(selected));print('Bot log:',session.log);print('vBot is in the game client. Commands: status | pause | resume | unload')
        commands=queue.Queue()
        def read_input():
            try:
                while True:commands.put(input().strip().lower())
            except EOFError:pass
        threading.Thread(target=read_input,daemon=True).start()
        while True:
            try:command=commands.get(timeout=.5)
            except queue.Empty:command='status'
            if command in ('quit','unload'):break
            if command not in ('status','pause','resume'):print('Use status, pause, resume or unload');continue
            status=session.control(command)
            if command!='status' or status.startswith('paused'):print(status)
    except KeyboardInterrupt:return 0
    except (OSError,ValueError,TimeoutError) as error:
        print(str(error),file=sys.stderr)
        if hasattr(session,'trace'):print('Startup trace:',session.trace,file=sys.stderr)
        if hasattr(session,'log'):print('Bot log:',session.log,file=sys.stderr)
        return 1
    finally:session.close()
    return 0

if __name__=='__main__':raise SystemExit(main())
