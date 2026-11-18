import json
from pathlib import Path
import sys
import tempfile
import unittest
sys.path.insert(0,str(Path(__file__).resolve().parents[1]/'external'))
from direct import DirectBroker, lua_literal, process_mailbox
from lupa.luajit20 import LuaRuntime

ROOT=Path(__file__).resolve().parents[1]
SELECTED={'pid':42,'target':'fixture.exe'}

class GameTransport:
    def __init__(self):
        self.lua=LuaRuntime(unpack_returned_tuples=True);self.closed=False;self.calls=[]
        self.lua.execute('''
clock=0;g_clock={millis=function()return clock end};callbacks={}
function connect(object,events)for key,fn in pairs(events)do callbacks[key]=fn end end
function disconnect()end
local methods={getName=function()return 'Ação' end,getPosition=function()return {x=10,y=20,z=7}end}
player=setmetatable({},{__index=methods})
walks=0;g_game={isOnline=function()return true end,getLocalPlayer=function()return player end,walk=function(d)walks=walks+1;direction=d;return true end,optional=function(a,b,c)return c end}
g_game.multiple=function()return nil,'second',false end;g_map={};function echo(text)return text end
''')
    def request(self,command):
        self.calls.append(command)
        try:
            result=self.lua.execute(command[5:]);return 'OK\n'+str(result)
        except Exception as e:return 'ERROR\n'+str(e)
    def close(self):self.closed=True

class DirectTests(unittest.TestCase):
    def setUp(self):
        self.transport=GameTransport();self.broker=DirectBroker(SELECTED,None,None,None,transport=self.transport)
    def tearDown(self):self.broker.close()
    def rpc(self,value):
        result=self.broker.rpc(value);self.assertTrue(result['ok'],result);return result.get('value')
    def function(self,root,key):return self.rpc({'op':'get','root':root,'key':key})['__vbp_ref']
    def test_real_luajit_calls_game_function_and_userdata_methods(self):
        get_player=self.function('g_game','getLocalPlayer')
        player=self.rpc({'op':'call','ref':get_player,'args':[]})
        get_name=self.rpc({'op':'get','ref':player['__vbp_ref'],'key':'getName'})
        name=self.rpc({'op':'call','ref':get_name['__vbp_ref'],'args':[player],'count':1})
        self.assertEqual(name,'Ação')
        self.rpc({'op':'call','ref':self.function('g_game','walk'),'args':[2],'count':1})
        self.assertEqual(self.transport.lua.globals().walks,1)
        self.assertEqual(self.transport.lua.globals().direction,2)
    def test_unicode_quotes_and_inert_source_text_are_preserved(self):
        text='Ação "quoted" \\ \n return dangerous()'
        self.assertEqual(self.rpc({'op':'call','root':'echo','args':[text],'count':1}),text)
    def test_events_and_expired_attachment_cleanup(self):
        self.transport.lua.execute("callbacks.onTextMessage(1,'hello')")
        batch=self.rpc({'op':'events'})
        self.assertEqual(batch['events'][0]['name'],'onTextMessage')
        self.assertEqual(batch['events'][0]['args'],[1,'hello'])
        self.assertEqual(self.rpc({'op':'events'})['events'],{})
        source=(ROOT/'external/game_bridge.lua').read_text()
        response=self.transport.request('exec return (function(...) '+source+' end)("second")')
        self.assertTrue(response.startswith('ERROR\n'))
        self.transport.lua.execute('clock=6000')
        self.assertTrue(self.transport.request('exec return (function(...) '+source+' end)("second")').startswith('OK\n'))
        with self.assertRaises(ValueError):self.broker.read()
        self.broker.closed=True;self.transport.close()
    def test_mailbox_exactly_once_expiry_and_nil_arguments(self):
        with tempfile.TemporaryDirectory() as folder:
            root=Path(folder)
            import time
            req={'seq':1,'token':self.broker.token,'expires':time.time()+3,'op':'call','ref':self.function('g_game','optional'),'args':{'1':'first','3':'third'},'count':3}
            (root/'request.json').write_text(json.dumps(req))
            self.assertEqual(process_mailbox(self.broker,root,0),1)
            self.assertEqual(json.loads((root/'response.json').read_text())['value'],'third')
            before=len(self.transport.calls);(root/'request.json').write_text(json.dumps(req))
            with self.assertRaises(ValueError):process_mailbox(self.broker,root,1)
            self.assertEqual(len(self.transport.calls),before)
            req.update(seq=2,expires=0);(root/'request.json').write_text(json.dumps(req));process_mailbox(self.broker,root,1)
            self.assertFalse(json.loads((root/'response.json').read_text())['ok'])
            self.assertEqual(len(self.transport.calls),before)
    def test_transport_timeout_closes_without_replay(self):
        def fail(_):raise TimeoutError('uncertain')
        self.transport.request=fail
        with self.assertRaises(TimeoutError):self.broker.read()
        self.assertTrue(self.broker.closed and self.transport.closed)
        with self.assertRaises(ValueError):self.broker.read()

if __name__=='__main__':unittest.main()

class FacadeTests(unittest.TestCase):
    def test_external_luajit_proxy_calls_selected_game_vm(self):
        import threading,time,queue
        from lupa.luajit20 import lua_type
        with tempfile.TemporaryDirectory() as folder:
            root=Path(folder);ready=queue.Queue();stop=threading.Event();failures=[]
            def worker():
                transport=GameTransport();broker=DirectBroker(SELECTED,None,None,None,transport=transport)
                ready.put(broker.token)
                try:
                    previous=0
                    while not stop.is_set():
                        previous=process_mailbox(broker,root,previous);time.sleep(.001)
                except Exception as e:failures.append(e)
                finally:broker.close()
            thread=threading.Thread(target=worker);thread.start();token=ready.get(timeout=2)
            ui=LuaRuntime(unpack_returned_tuples=True)
            def plain(v):
                if lua_type(v)!='table':return v
                keys=list(v.keys())
                if keys and all(type(k) is int and 1<=k<=len(keys) for k in keys):return [plain(v[i]) for i in range(1,len(keys)+1)]
                return {str(k):plain(x) for k,x in v.items()}
            ui.globals().encode_json=lambda v:json.dumps(plain(v))
            ui.globals().decode_json=lambda s:ui.table_from(json.loads(s),recursive=True)
            ui.globals().millis=lambda:time.monotonic()*1000
            ui.execute('json={encode=encode_json,decode=decode_json};g_clock={millis=millis}')
            try:
                code=(ROOT/'external/remote.lua').read_text()
                ui.execute('local VBP_RPC_PATH='+lua_literal(str(root))+';local VBP_TOKEN='+lua_literal(token)+'\n'+code)
                ui.execute('context={};externalRemote.install(context)')
                self.assertEqual(ui.eval('context.player:getName()'),'Ação')
                self.assertTrue(ui.eval('context.g_game.walk(3)'))
                self.assertEqual(ui.eval('context.player:getPosition().z'),7)
                self.assertEqual(ui.execute('return context.g_game.multiple()'),(None,'second',False))
                self.assertEqual(ui.eval('context.clientLua("return direction")'),3)
                self.assertEqual(ui.eval('context.clientGlobal("echo")("custom")'),'custom')
                self.assertEqual(ui.eval('context.g_game.optional("first",nil,"third")'),'third')
                ui.execute('context.clientLua([[callbacks.onTextMessage(1,nil,"third")]])')
                result=ui.execute('local a,b,c;externalRemote.poll({onTextMessage=function(x,y,z)a,b,c=x,y,z end});return a,b,c')
                self.assertEqual(result,(1,None,'third'))
            finally:
                stop.set();thread.join(timeout=3)
            self.assertFalse(thread.is_alive());self.assertEqual(failures,[])

class HostLifecycleTests(unittest.TestCase):
    def test_connected_macro_runner_pauses_and_resumes_without_old_client_refresh(self):
        lua=LuaRuntime(unpack_returned_tuples=True)
        lua.execute('''
queue={};runs=0;polls=0
local widget={on=false}
function widget:setOn(value)self.on=value end
function widget:isOn()return self.on end
function widget:setEnabled()end
function widget:setText()end
function widget:clearOptions()end
function widget:addOption()end
local tab={getText=function()return 'Main' end}
local tabs=setmetatable({current=tab},{__index=widget})
function tabs:getCurrentTab()return self.current end
function tabs:getTabs()return {tab} end
function tabs:selectTab(t)self.current=t end
contentsPanel={config=widget,enableButton=setmetatable({on=false},{__index=widget}),editConfig=widget,botTabs=tabs,statusLabel=widget}
botWindow=setmetatable({},{__index=function()return function()end end})
g_window={setMinimumSize=function()end,resize=function()end}
g_ui={getRootWidget=function()return {getChildren=function()return {}end}end}
g_resources={readFileContents=function()return 'fixture' end}
json={decode=function()return {name='Fixture',pid=42,position={x=1,y=2,z=7}} end}
g_logger={info=function()end,error=function(e)error(e)end}
function executeBot()return {script=function()runs=runs+1 end,callbacks={}}end
externalRemote={failed=false,poll=function()polls=polls+1 end}
function scheduleEvent(fn,ms)table.insert(queue,{fn=fn,ms=ms})end
function nextMacro()for i,e in ipairs(queue)do if e.ms==50 then table.remove(queue,i);e.fn();return end end;error('No macro timer')end
''')
        source=(ROOT/'external/host.lua').read_text().replace('local snapshotPath = nil','local snapshotPath = "fixture"')
        lua.execute(source);lua.execute('openExternalProfile();nextMacro()')
        self.assertEqual(lua.globals().runs,1)
        lua.execute('contentsPanel.enableButton:onClick();nextMacro()')
        self.assertEqual(lua.globals().runs,1)
        lua.execute('contentsPanel.enableButton:onClick();nextMacro()')
        self.assertEqual(lua.globals().runs,2)
