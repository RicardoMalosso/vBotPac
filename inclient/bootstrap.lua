-- SPEC is supplied by the launcher. The DLL supplies this compiler argument.
local nativeCompile,openPrivateFiles=...
assert(type(nativeCompile)=='function','Native compiler argument missing')
assert(type(openPrivateFiles)=='function','Private file adapter missing; restart the client with the updated DLL')
local privateFiles=assert(openPrivateFiles(SPEC.dataRoot))
-- A native UI/image read can terminate encrypted clients before Lua pcall can
-- catch an error. Reject an obsolete/unsafe package layout before any UI loads.
assert(type(SPEC.runtime)=='string' and SPEC.runtime:find('/bot/',1,true),'Unsafe native resource layout; update the launcher (runtime requires /bot/)')
assert(not _G.__vbotpac_module or _G.__vbotpac_module.closed,'vBotPac is already loaded; unload it first')
assert(type(setfenv)=='function','Lua 5.1 environments are required')
assert(g_game and g_game.isOnline() and g_game.getLocalPlayer(),'Select an online character')
assert(g_ui and g_ui.loadUIFromString and g_ui.importStyleFromString,'Client in-memory UI API unavailable')
assert(g_resources and g_resources.readFileContents,'Client resource API unavailable')
assert(modules and modules.game_interface,'Client game_interface module unavailable')
local S={token=SPEC.token,closed=false,paused=false,ready=false,errors={},compiled={},events={},connections={},widgets=setmetatable({},{__mode='k'}),ticks=0,last=g_clock.millis()}
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
local function file(op,name,...)return assert(privateFiles(op,name,...))end
local function log(level,text)
 local stamp=os.date('%Y-%m-%d %H:%M:%S')
 local ok,err=privateFiles('append',SPEC.log,stamp..' ['..level..'] '..tostring(text):sub(1,8192)..'\n')
 if not ok then S.error='Private log write failed: '..tostring(err) end
end
scope.g_logger={}
for _,level in ipairs({'trace','debug','info','warning','error'})do scope.g_logger[level]=function(text)log(level,text)end end
scope.g_logger.fatal=function(text)log('fatal',text);error(text,2)end
scope.print=function(...)local values={};for i=1,select('#',...)do values[i]=tostring(select(i,...))end;log('info',table.concat(values,'\t'))end
local function path(file)
 assert(type(file)=='string','Resource path must be a string')
 if file:sub(1,1)~='/' then file=runtime..'/'..file end
 file=file:gsub('/+','/')
 if file:sub(1,5)=='/bot/' then file=profile..file:sub(5)
 elseif file=='/bot' then file=profile
 elseif file:sub(1,18)=='/modules/game_bot/' then file=runtime..file:sub(18) end
 return file
end
-- Own resources use the DLL's private project directory. No client resource
-- mount, write directory, global logger or module search path is changed.
scope.g_resources=setmetatable({}, {__index=g_resources})
local function own(name)return name:sub(1,18)=='/vbotpac_inclient/' end
for name,op in pairs({readFileContents='read',writeFileContents='write',fileExists='exists',directoryExists='directory',makeDir='mkdir',deleteFile='remove'})do
 local method=name
 scope.g_resources[name]=function(name,...)
  name=path(name)
  if own(name)then
   local value,err=privateFiles(op,name,...)
   if value==false and not err then return false end
   assert(value,err);return value
  end
  if op~='read' and op~='exists' and op~='directory' then error('Writes must stay in the private bot directory') end
  return g_resources[method](name,...)
 end
end
scope.g_resources.getWriteDir=function()return SPEC.dataRoot end
function scope.g_resources.listDirectoryFiles(name,full,raw)
 name=path(name)
 if not own(name)then return g_resources.listDirectoryFiles(name,full,raw)end
 local result={};for entry in file('list',name):gmatch('([^%z]+)%z')do result[#result+1]=full and name:gsub('/$','')..'/'..entry or entry end
 return result
end
scope.g_ui=setmetatable({}, {__index=g_ui})
local function track(widget)
 if widget then
  S.widgets[widget]=true
  if type(widget.getChildren)=='function' then
   for _,child in ipairs(widget:getChildren())do
    local id=child:getId();if id~='' and widget[id]==nil then widget[id]=child end
    track(child)
   end
  end
 end
 return widget
end
if modules.client_topmenu then
 scope.modules.client_topmenu=setmetatable({}, {__index=modules.client_topmenu})
 function scope.modules.client_topmenu.addRightGameToggleButton(...)
  return track(modules.client_topmenu.addRightGameToggleButton(...))
 end
end
local dynamicStyles={}
local function style(name)return (SPEC.styles and SPEC.styles[name] or dynamicStyles[name])and 'VBPM_'..name or name end
local function uiText(text)
 for name in text:gmatch('[\r\n]%s*#?([%w_]+)[ \t]*<')do dynamicStyles[name]=true end
 local first=text:match('^%s*#?([%w_]+)[ \t]*<');if first then dynamicStyles[first]=true end
 local lines={}
 for line in (text..'\n'):gmatch('(.-)\n')do
  local space,name,base=line:match('^(%s*)#?([%w_]+)%s*<%s*([%w_]+)%s*$')
  if name then line=space..'#'..style(name)..' < '..style(base)
  else
   space,name=line:match('^(%s*)([%w_]+)%s*$')
   if name then line=space..style(name)end
  end
  local font=line:match('^%s*font:%s*([^%s]+)%s*$')
  if not font or (g_fonts and g_fonts.fontExists and g_fonts.fontExists(font))then lines[#lines+1]=line end
 end
 return table.concat(lines,'\n')
end
function scope.g_ui.importStyleFromString(text)
 assert(g_ui.importStyleFromString(uiText(text)),'Failed to import private UI styles')
 return true
end
function scope.g_ui.importStyle(name)
 name=path(name);if not name:match('%.otui$')then name=name..'.otui'end
 return scope.g_ui.importStyleFromString(scope.g_resources.readFileContents(name))
end
function scope.g_ui.loadUIFromString(text,parent)
 return track(assert(g_ui.loadUIFromString(uiText(text),parent),'Failed to create private UI'))
end
function scope.g_ui.loadUI(name,parent)
 name=path(name);if not name:match('%.otui$')then name=name..'.otui'end
 return scope.g_ui.loadUIFromString(scope.g_resources.readFileContents(name),parent)
end
function scope.g_ui.displayUI(name,parent)
 local widget=scope.g_ui.loadUI(name,parent or g_ui.getRootWidget())
 widget:show();widget:raise();widget:focus();return widget
end
function scope.g_ui.createWidget(name,...)
 local widget=track(assert(g_ui.createWidget(style(name),...),'Failed to create private widget: '..name))
 if name:match('Window$') and widget.closeButton==nil then
  if type(widget.recursiveGetChildById)=='function' then widget.closeButton=widget:recursiveGetChildById('closeButton')end
  if widget.closeButton==nil then
   local ids={};if type(widget.getChildren)=='function' then for _,child in ipairs(widget:getChildren())do ids[#ids+1]=child:getId()end end
   log('warning','Window '..name..' has no closeButton; children: '..table.concat(ids,','))
  end
 end
 return widget
end
if g_ui.getStyle then scope.g_ui.getStyle=function(name)return g_ui.getStyle(style(name))end end
if g_ui.getStyleClass then scope.g_ui.getStyleClass=function(name)return g_ui.getStyleClass(style(name))end end
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
 if not scope.g_resources.fileExists(resolved) and resolved:sub(-4)~='.lua' then resolved=resolved..'.lua' end
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
 local function finish(ok,...)
  if not ok then S.fail((...));return end
  return ...
 end
 return function(...)
  if S.closed then return end
  return finish(pcall(fn,...))
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
 local handle=cycleEvent(guarded(fn),delay)
 S.events[handle]=fn;return handle
end
function S.message(text)
 S.error=tostring(text):sub(1,2048)
 if scope.contentsPanel then scope.contentsPanel.statusLabel:setText(S.error) end
 log('warning',S.error)
end
function S.fail(text)S.paused=true;S.errors[#S.errors+1]=tostring(text):sub(1,2048);if #S.errors>32 then table.remove(S.errors,1) end;S.message(text)end
-- Only this runtime's settings node is redirected; ordinary client settings
-- (profile number, layout, etc.) retain their real game values.
scope.g_settings=setmetatable({}, {__index=g_settings})
local settings={}
if privateFiles('exists',SPEC.settings)then settings=json.decode(file('read',SPEC.settings))end
function scope.g_settings.getNode(key)
 if key~='bot' then return settings[key] or g_settings.getNode(key)end
 local value=settings.bot or {}
 local index=g_game.getCharacterName()..'_'..g_game.getClientVersion()
 value[index]=value[index] or {enabled=true,config='vBotPac'}
 settings.bot=value
 return value
end
function scope.g_settings.setNode(key,value)settings[key]=value end
function scope.g_settings.save()file('write',SPEC.settings,json.encode(settings));return true end
local function save()if scope.save then pcall(scope.save) end end
local function trace()
 if json and json.encode then pcall(function()
  file('write',SPEC.trace,json.encode({ready=S.ready,errors=S.errors,compiled=S.compiled,ticks=S.ticks,registryVerified=S.registryBefore~=nil,registryUnchanged=S.registryBefore~=nil and S.registryBefore==registry()}))
 end) end
end
function S.unload()
 if S.closed then return 'already unloaded' end
 S.closed=true;S.paused=true;save()
 pcall(scope.g_settings.save)
 trace()
 if scope.terminate then pcall(scope.terminate) end
 for event in pairs(S.events)do pcall(removeEvent,event) end;S.events={}
 for i=#S.connections,1,-1 do local c=S.connections[i];pcall(disconnect,c.object,c.callbacks) end;S.connections={}
 for widget in pairs(S.widgets)do pcall(function()widget:destroy()end) end;S.widgets={}
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
 return S.widgets[widget]==true
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
 track(scope.botButton)
 scope.botWindow:open();scope.botWindow:raise();scope.botButton:setOn(true)
 local function watchdog()
  if S.closed then return end
  if g_clock.millis()-S.last>5000 then S.unload();return end
  scope.scheduleEvent(watchdog,100)
 end
 scope.scheduleEvent(watchdog,100)
end)
if not ok then S.errors[#S.errors+1]=tostring(failure);S.unload();error(failure) end
return 'loaded; compiled='..#S.compiled
