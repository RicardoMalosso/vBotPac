import ctypes
import json
from pathlib import Path
import sys
import tempfile
import unittest
from unittest.mock import patch
ROOT=Path(__file__).resolve().parents[1]
sys.path.insert(0,str(ROOT/'inclient'))
from package import stage, adapt, files
from launch import ModuleSession


class PackageTests(unittest.TestCase):
    def test_complete_runtime_is_staged_privately_and_keeps_saved_config(self):
        with tempfile.TemporaryDirectory() as folder:
            selected={'sha256':'a'*64}
            spec=stage(folder,selected,'Player')
            root=Path(folder)
            self.assertTrue((root/spec['runtime'].lstrip('/')/'executor.lua').is_file())
            runtime=root/spec['runtime'].lstrip('/')
            for file in ('bot.lua','edit.otui','bot.otui','functions/player.lua','functions/map.lua','panels/waypoints.lua'):
                self.assertTrue((runtime/file).is_file(),file)
            self.assertFalse(list(root.rglob('*.otmod')))
            bot=(runtime/'bot.lua').read_text()
            self.assertIn('function vbpIsRunning()',bot)
            self.assertIn('ownsWidget(widget)',bot)
            ui=(runtime/'bot.otui').read_text()
            self.assertIn('_G.__vbotpac_module.scope.',ui)
            self.assertNotIn('modules.game_bot.',ui)
            saved=root/spec['profile'].lstrip('/')/'vBotPac/storage/profile_0.json'
            saved.parent.mkdir(exist_ok=True);saved.write_text('{"saved":true}')
            stage(folder,selected,'Player')
            self.assertEqual(saved.read_text(),'{"saved":true}')
            self.assertEqual(len(spec['helpers']['functions']),19)
            self.assertGreater(len(spec['helpers']['panels']),5)
    def test_native_capability_failure_never_falls_back_to_exec(self):
        class Transport:
            calls=[]
            def request(self,command):self.calls.append(command);return 'ERROR\nUnknown command'
        t=Transport();session=ModuleSession(t)
        with self.assertRaisesRegex(ValueError,'Restart the game'):session.bind()
        self.assertEqual(t.calls,['module-capabilities'])
    def test_explicit_binding_and_module_protocol_are_used(self):
        class Transport:
            def __init__(self):self.calls=[]
            def request(self,command):
                self.calls.append(command)
                if command=='module-capabilities':return 'OK\nvBotPac module loader v1'
                if command.startswith('atomic-bind '):return '{"status":"bound"}'
                return 'OK\nvalue'
            def close(self):pass
        t=Transport();s=ModuleSession(t,'a'*32);s.bind()
        self.assertEqual(s.request('return 1'),'value')
        self.assertEqual(t.calls[-1],'module-v1 '+'a'*32+' return 1')
        s.close()


if __name__=='__main__':unittest.main()

class NamespaceTests(unittest.TestCase):
    """Small lifecycle fixture, not a claim of complete vBot/UI startup."""
    def fixture(self,fail=False):
        from lupa.luajit20 import LuaRuntime
        from launch import literal
        lua=LuaRuntime(unpack_returned_tuples=True)
        lua.execute('''
now=0;steps=0;queue={};nextId=0;nativeCompiler=loadstring
function load()error('client load wrapper used')end;loadstring=load
player={};g_clock={millis=function()return now end}
g_game={isOnline=function()return true end,getLocalPlayer=function()return player end,getCharacterName=function()return 'fixture' end,getClientVersion=function()return 740 end,walk=function(d)steps=steps+d end}
local module={getName=function()return 'game_interface' end}
g_modules={getModules=function()return {module}end}
existingBot={marker='existing'};modules={game_interface={},game_bot=existingBot}
g_settings={getNode=function()return {}end,setNode=function()end}
g_logger={info=function()end,warning=function()end}
json={encode=function()return '{}' end}
function connect()end;function disconnect()end
function scheduleEvent(fn,ms)nextId=nextId+1;queue[nextId]={fn=fn,ms=ms};return nextId end
function removeEvent(id)queue[id]=nil end
function runTimer(ms)for id,event in pairs(queue)do if event.ms==ms then queue[id]=nil;event.fn();return end end;error('Missing timer')end
widget={destroy=function()destroyed=true end,open=function()end,raise=function()end,setOn=function()end}
g_ui={loadUI=function()return widget end,importStyle=function()end}
resources={};g_resources={fileExists=function(file)return resources[file]~=nil end,readFileContents=function(file)return assert(resources[file],file)end,writeFileContents=function()end}
''')
        code='''
local running=false
privateGlobal='private'
function init()botWindow=g_ui.loadUI('fixture');botButton=botWindow;scheduleEvent(refresh,20)end
function createDefaultConfigs()end
function clear()running=false end
function offline()clear()end
function terminate()clear()end
function save()end
function onError()end
function message()end
function refresh()
 onContextReady({player=g_game.getLocalPlayer()})
 running=true;check()
end
function check()g_game.walk(1);scheduleEvent(check,10)end
function vbpIsRunning()return running end
function vbpSetEnabled(value)if value then refresh()else clear()end end
'''
        if fail:code+='\nfunction init()botWindow=g_ui.loadUI("fixture");error("fixture failure")end\n'
        lua.globals().resources['/runtime/bot.lua']=code
        spec={'token':'t','runtime':'/runtime','profile':'/private/bot','trace':'/trace.json','helpers':{}}
        source='local SPEC='+literal(spec)+'\n'+(ROOT/'inclient/bootstrap.lua').read_text()
        return lua,source
    def test_private_namespace_uses_real_globals_and_keeps_registry_unchanged(self):
        lua,source=self.fixture();result=lua.eval('function(...) '+source+' end')(lua.globals().nativeCompiler)
        self.assertTrue(result.startswith('loaded'))
        self.assertTrue(lua.eval('modules.game_bot==existingBot'))
        self.assertIsNone(lua.eval('_G.privateGlobal'))
        self.assertEqual(lua.eval('__vbotpac_module.scope.privateGlobal'),'private')
        lua.execute('runTimer(50)');self.assertEqual(lua.globals().steps,1)
        self.assertTrue(lua.eval('__vbotpac_module.control("t","pause")').startswith('paused'))
        self.assertTrue(lua.eval('__vbotpac_module.control("t","resume")').startswith('running'))
        self.assertEqual(lua.globals().steps,2)
        lua.eval('__vbotpac_module.control("t","unload")')
        self.assertIsNone(lua.globals().__vbotpac_module)
        self.assertTrue(lua.globals().destroyed)
    def test_startup_failure_rolls_back_only_its_namespace_and_widgets(self):
        lua,source=self.fixture(True)
        with self.assertRaisesRegex(Exception,'fixture failure'):lua.eval('function(...) '+source+' end')(lua.globals().nativeCompiler)
        self.assertIsNone(lua.globals().__vbotpac_module)
        self.assertTrue(lua.eval('modules.game_bot==existingBot'))
        self.assertTrue(lua.globals().destroyed)
