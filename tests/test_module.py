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
    def test_old_profile_data_is_copied_without_writing_to_the_client(self):
        with tempfile.TemporaryDirectory() as legacy, tempfile.TemporaryDirectory() as own:
            old=stage(legacy,{'sha256':'a'*64},'Player')
            saved=Path(legacy)/old['profile'].lstrip('/')/'vBotPac/storage/profile_0.json'
            saved.parent.mkdir(exist_ok=True);saved.write_text('{"saved":true}')
            spec=stage(own,{'sha256':'a'*64},'Player',legacy_dir=legacy)
            migrated=Path(own)/spec['profile'].lstrip('/')/'vBotPac/storage/profile_0.json'
            self.assertEqual(migrated.read_text(),saved.read_text())
            migrated.write_text('{"saved":"new"}')
            stage(own,{'sha256':'a'*64},'Player',legacy_dir=legacy)
            self.assertEqual(migrated.read_text(),'{"saved":"new"}')
            self.assertEqual(saved.read_text(),'{"saved":true}')
            runtime=Path(own)/spec['runtime'].lstrip('/')
            self.assertIn('image-source-base64:',(runtime/'edit.otui').read_text())
            self.assertIn('&save: false',(runtime/'bot.otui').read_text())
    def test_native_ui_and_image_resources_use_the_plaintext_bot_tree(self):
        with tempfile.TemporaryDirectory() as folder:
            spec=stage(folder,{'sha256':'a'*64},'Player')
            root=Path(folder)
            native_assets=[]
            for source,relative in files():
                prefix=spec['runtime'] if relative.startswith('runtime/') else spec['profile']
                virtual=prefix+'/'+relative[8:]
                if source.suffix in ('.otui','.png'):
                    # Same path rule used by the native resource reader. A Lua
                    # pcall cannot recover from its fatal decrypt-file branch.
                    self.assertIn('/bot/',virtual,virtual)
                    self.assertTrue((root/virtual.lstrip('/')).is_file(),virtual)
                    native_assets.append(virtual)
            self.assertGreater(len(native_assets),20)
            self.assertTrue(any(x.endswith('/ui/basic.otui') for x in native_assets))
            self.assertTrue(any(x.endswith('/configs.png') for x in native_assets))
            self.assertFalse((root/'bot').exists(),'Private runtime must not appear as a client bot configuration')

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
            self.assertIn('widget.botWidget and ownsWidget(widget)',bot)
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
                if command=='module-capabilities':return 'OK\nvBotPac module loader v2; private-files'
                if command.startswith('atomic-bind '):return '{"status":"bound"}'
                return 'OK\nvalue'
            def close(self):pass
        t=Transport();s=ModuleSession(t,'a'*32);s.bind()
        self.assertEqual(s.request('return 1'),'value')
        self.assertEqual(t.calls[-1],'module-v1 '+'a'*32+' return 1')
        s.close()


class RuntimeAdaptationTests(unittest.TestCase):
    def test_real_executor_inherits_client_globals_and_keeps_profile_writes_private(self):
        from lupa.luajit20 import LuaRuntime
        lua=LuaRuntime(unpack_returned_tuples=True)
        lua.execute('''
clientGlobals=_G;steps=0;spoken='';customClientFunction=function()return 42 end
G={};g_game={getLocalPlayer=function()return {}end,walk=function(n)steps=steps+n end,talk=function(s)spoken=s end}
g_clock={millis=function()return 0 end};g_app={};g_platform={};g_ui={createWidget=function()return {}end}
function dofiles()end
function string:split()return {'fixture','lua'}end
g_resources={listDirectoryFiles=function()return {'fixture.lua'}end,readFileContents=function()return [[
 assert(_G==clientGlobals and customClientFunction()==42)
 assert(g_game==_G.g_game)
 g_game.walk(2);g_game.talk('hello');privateProfileValue='private'
]] end}
tabs={addTab=function()return {tabPanel={content={}}}end}
function onContextReady(context)loadedContext=context end
''')
        executor=adapt('executor.lua',(ROOT/'vendor/game_bot/executor.lua').read_text(),'/runtime','/private/bot')
        lua.execute(executor)
        lua.execute("executeBot('vBotPac',{},tabs,function()end,function()end,function()end,{})")
        self.assertEqual(lua.globals().steps,2)
        self.assertEqual(lua.globals().spoken,'hello')
        self.assertIsNone(lua.eval('_G.privateProfileValue'))
        self.assertEqual(lua.eval('loadedContext.privateProfileValue'),'private')

    def test_original_game_bot_clear_keeps_host_windows_and_foreign_bot_widgets(self):
        from lupa.luajit20 import LuaRuntime
        lua=LuaRuntime(unpack_returned_tuples=True)
        lua.execute('''
local function widget(extra)
 local value=extra or {};value.destroy=function(self)self.destroyed=true end
 for _,name in ipairs({'setup','hide','show','setOn','setContentWidget','clearTabs','destroyChildren','updateLayout','unlockVisibleFloor'})do value[name]=function()end end
 return value
end
profileWindow=widget({botWidget=true});foreignWindow=widget({botWidget=true})
bot=widget({contentsPanel={config={},enableButton={},statusLabel={},messages=widget(),botTabs=widget()}})
editor=widget();button=widget();owned={[profileWindow]=true,[bot]=true,[editor]=true}
function ownsWidget(value)return owned[value]==true end
local left={getChildren=function()return {bot}end};local empty={getChildren=function()return {}end}
g_ui={importStyle=function()end,loadUI=function()return bot end,displayUI=function()return editor end,
 getRootWidget=function()return {getChildren=function()return {editor,profileWindow,foreignWindow}end}end}
modules={client_topmenu={addRightGameToggleButton=function()return button end},game_interface={
 getLeftPanel=function()return left end,getRightPanel=function()return empty end,gameMapPanel=empty,getMapPanel=function()return widget()end}}
g_game={isOnline=function()return true end,enableTileThingLuaCallback=function()error('Shared callback disabled')end}
function connect()end;function disconnect()end;function dofile()end;function scheduleEvent()end;function removeEvent()end
function tr(value)return value end
''')
        lua.execute(adapt('bot.lua',(ROOT/'vendor/game_bot/bot.lua').read_text(),'/runtime','/private/bot'))
        lua.execute('init()')
        self.assertTrue(lua.eval('profileWindow.destroyed'))
        self.assertIsNone(lua.eval('foreignWindow.destroyed'))
        self.assertIsNone(lua.eval('bot.destroyed'))
        self.assertIsNone(lua.eval('editor.destroyed'))

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
g_settings={getNode=function()return {}end,setNode=function()error('Client settings write')end,save=function()error('Client settings save')end}
g_logger={info=function()error('Client logger used')end,warning=function()error('Client logger used')end,error=function()error('Client logger used')end}
json={encode=function()return '{}' end}
function connect(object,callbacks)lastCallbacks=callbacks end;function disconnect()end
function scheduleEvent(fn,ms)nextId=nextId+1;queue[nextId]={fn=fn,ms=ms};return nextId end
function removeEvent(id)queue[id]=nil end
function runTimer(ms)for id,event in pairs(queue)do if event.ms==ms then queue[id]=nil;event.fn();return end end;error('Missing timer')end
widget={destroy=function()destroyed=true end,open=function()end,raise=function()end,setOn=function()end}
g_ui={loadUI=function()error('Client file UI used')end,importStyle=function()error('Client file style used')end,
 loadUIFromString=function()return widget end,importStyleFromString=function()return true end}
resources={};g_resources={readFileContents=function()error('Client resource reader used')end,writeFileContents=function()error('Client resource writer used')end}
function nativeFiles(root)
 return function(op,name,data)
  if op=='exists' then return resources[name]~=nil end
  if op=='read' then return assert(resources[name],name)end
  if op=='write' then resources[name]=data;return true end
  if op=='append' then resources[name]=(resources[name]or '')..data;return true end
  error('Unexpected operation '..op)
 end
end
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
        lua.globals().resources['/vbotpac_inclient/bot/runtime/fixture/bot.lua']=code
        lua.globals().resources['/vbotpac_inclient/bot/runtime/fixture/fixture.otui']='UIWidget'
        spec={'token':'t','runtime':'/vbotpac_inclient/bot/runtime/fixture','profile':'/vbotpac_inclient/profiles/fixture/bot','trace':'/trace.json','settings':'/settings.json','log':'/inclient.log','dataRoot':'/private/project','styles':{},'helpers':{}}
        source='local SPEC='+literal(spec)+'\n'+(ROOT/'inclient/bootstrap.lua').read_text()
        return lua,source
    def test_legacy_encrypted_resource_layout_is_rejected_before_ui_creation(self):
        lua,source=self.fixture()
        source=source.replace('"/vbotpac_inclient/bot/runtime/fixture"','"/vbotpac_inclient/runtime/legacy"')
        with self.assertRaisesRegex(Exception,'Unsafe native resource layout'):
            lua.eval('function(...) '+source+' end')(lua.globals().nativeCompiler,lua.globals().nativeFiles)
        self.assertIsNone(lua.globals().__vbotpac_module)
        self.assertIsNone(lua.globals().destroyed)
    def test_private_namespace_uses_real_globals_and_keeps_registry_unchanged(self):
        lua,source=self.fixture();result=lua.eval('function(...) '+source+' end')(lua.globals().nativeCompiler,lua.globals().nativeFiles)
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
    def test_signal_callbacks_preserve_return_values(self):
        lua,source=self.fixture();lua.eval('function(...) '+source+' end')(lua.globals().nativeCompiler,lua.globals().nativeFiles)
        lua.execute('__vbotpac_module.scope.connect(g_game,{onTest=function()return true,42 end})')
        self.assertEqual(lua.eval('lastCallbacks.onTest()'),(True,42))
    def test_logs_settings_and_resources_do_not_use_client_writers_or_logger(self):
        lua,source=self.fixture();lua.eval('function(...) '+source+' end')(lua.globals().nativeCompiler,lua.globals().nativeFiles)
        lua.execute('''
local scope=__vbotpac_module.scope
scope.print('private diagnostic');scope.g_logger.error('private error')
scope.g_settings.setNode('bot',{saved=true});scope.g_settings.save()
scope.g_resources.writeFileContents('/bot/fixture.cfg','private file')
''')
        self.assertIn('private diagnostic',lua.globals().resources['/inclient.log'])
        self.assertIn('private error',lua.globals().resources['/inclient.log'])
        self.assertEqual(lua.globals().resources['/settings.json'],'{}')
        self.assertEqual(lua.globals().resources['/vbotpac_inclient/profiles/fixture/bot/fixture.cfg'],'private file')
    def test_private_window_styles_fonts_and_child_binding(self):
        lua,source=self.fixture();lua.eval('function(...) '+source+' end')(lua.globals().nativeCompiler,lua.globals().nativeFiles)
        lua.execute('''
g_fonts={fontExists=function(name)return name~='cipsoftFont'end}
g_ui.importStyleFromString=function(text)importedStyle=text;return true end
g_ui.createWidget=function(name)
 assert(name=='VBPM_ExtrasWindow')
 return {getChildren=function()return {{getId=function()return 'closeButton'end,getChildren=function()return {}end}}end}
end
__vbotpac_module.scope.g_ui.importStyleFromString([[ExtrasWindow < UIWidget
  Button
    id: closeButton
    font: cipsoftFont]])
createdWindow=__vbotpac_module.scope.g_ui.createWidget('ExtrasWindow')
''')
        self.assertIn('#VBPM_ExtrasWindow < UIWidget',lua.globals().importedStyle)
        self.assertNotIn('cipsoftFont',lua.globals().importedStyle)
        self.assertTrue(lua.eval('createdWindow.closeButton~=nil'))
    def test_startup_failure_rolls_back_only_its_namespace_and_widgets(self):
        lua,source=self.fixture(True)
        with self.assertRaisesRegex(Exception,'fixture failure'):lua.eval('function(...) '+source+' end')(lua.globals().nativeCompiler,lua.globals().nativeFiles)
        self.assertIsNone(lua.globals().__vbotpac_module)
        self.assertTrue(lua.eval('modules.game_bot==existingBot'))
        self.assertTrue(lua.globals().destroyed)


if __name__=='__main__':unittest.main()
