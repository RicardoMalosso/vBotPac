-- SPEC is supplied by the launcher. The DLL supplies this compiler argument.
local nativeCompile=...
assert(type(nativeCompile)=='function','Native compiler argument missing')
assert(not _G.__vbotpac_module or _G.__vbotpac_module.closed,'vBotPac is already loaded; unload it first')
assert(type(setfenv)=='function','Lua 5.1 environments are required')
assert(g_game and g_game.isOnline() and g_game.getLocalPlayer(),'Select an online character')
assert(g_ui and g_ui.loadUI and g_ui.importStyle,'Client UI API unavailable')
assert(g_resources and g_resources.readFileContents,'Client resource API unavailable')
assert(modules and modules.game_interface,'Client game_interface module unavailable')
local S={token=SPEC.token,closed=false,paused=false,ready=false,errors={},compiled={},events={},connections={},widgets={},ticks=0,last=g_clock.millis()}
local function registry()
 if not g_modules or type(g_modules.getModules)~='function' then return nil end
 local names={};for _,entry in pairs(g_modules.getModules())do names[#names+1]=entry:getName() end
 table.sort(names);return table.concat(names,'\n')
end
S.registryBefore=registry()
local scope=setmetatable({G={},profileDirectory=SPEC.profile},{__index=_G})
S.scope=scope
scope.modules=setmetatable({game_bot=scope},{__index=modules})
local profile=SPEC.profile
local runtime=SPEC.runtime
local function path(file)
 assert(type(file)=='string','Resource path must be a string')
 if file:sub(1,1)~='/' then file=runtime..'/'..file end
 file=file:gsub('/+','/')
 if file:sub(1,5)=='/bot/' then file=profile..file:sub(5)
 elseif file=='/bot' then file=profile
 elseif file:sub(1,18)=='/modules/game_bot/' then file=runtime..file:sub(18) end
 return file
end
-- Resources are staged under the existing client write directory. No module
-- manifests or search paths are installed in the client's module manager.
scope.g_resources=setmetatable({}, {__index=g_resources})
for _,name in ipairs({'readFileContents','writeFileContents','fileExists','directoryExists','makeDir','deleteFile'})do
 if type(g_resources[name])=='function' then scope.g_resources[name]=function(file,...)return g_resources[name](path(file),...)end end
end
function scope.g_resources.listDirectoryFiles(file,...)
 return g_resources.listDirectoryFiles(path(file),...)
end
scope.g_ui=setmetatable({}, {__index=g_ui})
local function track(widget)if widget then S.widgets[#S.widgets+1]=widget end;return widget end
function scope.g_ui.importStyle(file)return g_ui.importStyle(path(file))end
function scope.g_ui.loadUI(file,parent)return track(g_ui.loadUI(path(file),parent))end
function scope.g_ui.displayUI(file,...)return track(g_ui.displayUI(path(file),...))end
function scope.g_ui.createWidget(...)return track(g_ui.createWidget(...))end
function scope.load(source,name,mode,environment)
 local chunk,err=nativeCompile(source,name or '=vBotPac.dynamic')
 if not chunk then return nil,err end
 setfenv(chunk,environment or scope)
 S.compiled[#S.compiled+1]=name or '=vBotPac.dynamic'
 return chunk
end
scope.loadstring=scope.load
function scope.loadfile(file)
 local resolved=path(file)
 if not g_resources.fileExists(resolved) and resolved:sub(-4)~='.lua' then resolved=resolved..'.lua' end
 return scope.load(scope.g_resources.readFileContents(resolved),'@'..resolved,nil,scope)
end
function scope.dofile(file)return assert(scope.loadfile(file))()end
function scope.dofiles(directory)
 local files={}
 for _,file in ipairs(SPEC.helpers[directory] or {})do files[#files+1]=file end
 table.sort(files)
 for _,file in ipairs(files)do scope.dofile(file) end
end
local function guarded(fn)
 return function(...)
  if S.closed then return end
  local ok,err=pcall(fn,...)
  if not ok then S.fail(err) end
 end
end
function scope.connect(object,callbacks)
 local copy={};for name,fn in pairs(callbacks)do copy[name]=guarded(fn) end
 connect(object,copy);S.connections[#S.connections+1]={object=object,callbacks=copy,original=callbacks}
end
function scope.disconnect(object,callbacks)
 for i=#S.connections,1,-1 do local c=S.connections[i]
  if c.object==object and c.original==callbacks then pcall(disconnect,c.object,c.callbacks);table.remove(S.connections,i) end
 end
end
function scope.scheduleEvent(fn,delay)
 local handle
 handle=scheduleEvent(function()S.events[handle]=nil;if not S.closed then fn() end end,delay)
 S.events[handle]=fn;return handle
end
scope.addEvent=function(fn)return scope.scheduleEvent(fn,0)end
function scope.removeEvent(handle)if handle then S.events[handle]=nil;return removeEvent(handle) end end
function scope.cycleEvent(fn,delay)
 local current;local function run()if not S.closed then fn();current=scope.scheduleEvent(run,delay) end end
 current=scope.scheduleEvent(run,delay);return current
end
function S.message(text)
 S.error=tostring(text):sub(1,2048)
 if scope.contentsPanel then scope.contentsPanel.statusLabel:setText(S.error) end
 if g_logger then g_logger.warning('[vBotPac] '..S.error) end
end
function S.fail(text)S.paused=true;S.errors[#S.errors+1]=tostring(text):sub(1,2048);if #S.errors>32 then table.remove(S.errors,1) end;S.message(text)end
-- Only this runtime's settings node is redirected; ordinary client settings
-- (profile number, layout, etc.) retain their real game values.
scope.g_settings=setmetatable({}, {__index=g_settings})
function scope.g_settings.getNode(key)
 if key~='bot' then return g_settings.getNode(key) end
 local value=g_settings.getNode('vbotpac_inclient') or {}
 local index=g_game.getCharacterName()..'_'..g_game.getClientVersion()
 value[index]=value[index] or {enabled=true,config='vBotPac'}
 return value
end
function scope.g_settings.setNode(key,value)return g_settings.setNode(key=='bot' and 'vbotpac_inclient' or key,value)end
local function save()if scope.save then pcall(scope.save) end end
local function trace()
 if json and json.encode then pcall(function()
  g_resources.writeFileContents(SPEC.trace,json.encode({ready=S.ready,errors=S.errors,compiled=S.compiled,ticks=S.ticks,registryVerified=S.registryBefore~=nil,registryUnchanged=S.registryBefore~=nil and S.registryBefore==registry()}))
 end) end
end
function S.unload()
 if S.closed then return 'already unloaded' end
 S.closed=true;S.paused=true;save()
 trace()
 if scope.terminate then pcall(scope.terminate) end
 for event in pairs(S.events)do pcall(removeEvent,event) end;S.events={}
 for i=#S.connections,1,-1 do local c=S.connections[i];pcall(disconnect,c.object,c.callbacks) end;S.connections={}
 for i=#S.widgets,1,-1 do pcall(function()S.widgets[i]:destroy()end) end;S.widgets={}
 if _G.__vbotpac_module==S then _G.__vbotpac_module=nil end
 return 'unloaded'
end
function S.control(token,operation)
 assert(token==S.token and not S.closed,'Attachment changed')
 S.last=g_clock.millis()
 if operation=='unload' then return S.unload() end
 if operation=='pause' then S.paused=true;scope.vbpSetEnabled(false)
 elseif operation=='resume' then assert(S.ready,'Profile did not finish loading');S.paused=false;scope.vbpSetEnabled(true) end
 return (S.paused and 'paused' or 'running')..'; compiled='..#S.compiled..'; ticks='..S.ticks..(S.error and '; '..S.error or '')
end
scope.onContextReady=function(context)S.context=context end
function scope.ownsWidget(widget)
 for _,owned in ipairs(S.widgets)do if widget==owned then return true end end
 return false
end
_G.__vbotpac_module=S
local ok,failure=pcall(function()
 scope.dofile('bot.lua')
 -- Use the complete game_bot loader, config manager, UI, helpers and callbacks.
 -- The fork profile has already been staged; importing unrelated default
 -- profiles on the game thread is unnecessary.
 scope.createDefaultConfigs=function()end
 local oldError=scope.onError
 scope.onError=function(text)S.fail(text);return oldError(text)end
 local oldMessage=scope.message
 scope.message=function(kind,text)
  if kind=='error' then S.fail(text) end
  return oldMessage(kind,text)
 end
 local oldCheck=scope.check
 scope.check=function()
  if S.closed then return end
  if not S.ready then scope.scheduleEvent(scope.check,50);return end
  if S.paused then return end
  S.ticks=S.ticks+1;return oldCheck()
 end
 local oldRefresh=scope.refresh
 scope.refresh=function()
  S.paused=false
  local result=oldRefresh()
  S.paused=not scope.vbpIsRunning()
  return result
 end
 local oldOffline=scope.offline
 scope.offline=function(...)S.paused=true;return oldOffline(...)end
 scope.init()
 -- init() schedules an online refresh. Run it synchronously once so the
 -- launcher can verify that every profile script finished before enabling ticks.
 local pending={};for event,fn in pairs(S.events)do if fn==scope.refresh then pending[#pending+1]=event end end
 for _,event in ipairs(pending)do scope.removeEvent(event) end
 scope.refresh()
 assert(scope.vbpIsRunning() and S.context and #S.errors==0,S.errors[1] or 'game_bot did not complete vBot startup')
 S.ready=true;S.paused=false
 assert(S.registryBefore==registry(),'Client module registry changed during startup')
 trace()
 if scope.botButton then S.widgets[#S.widgets+1]=scope.botButton end
 scope.botWindow:open();scope.botWindow:raise();scope.botButton:setOn(true)
 local function watchdog()
  if S.closed then return end
  if g_clock.millis()-S.last>5000 then S.unload();return end
  scope.scheduleEvent(watchdog,100)
 end
 scope.scheduleEvent(watchdog,100)
end)
if not ok then S.unload();error(failure) end
return 'loaded; compiled='..#S.compiled
