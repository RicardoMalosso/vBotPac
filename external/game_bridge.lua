-- Runs in the selected game's existing LuaJIT, on its owning thread.
local token=...
if __vbotpac_bridge then
 assert(g_clock.millis()-__vbotpac_bridge.last>5000,'Another vBotPac attachment is active')
 __vbotpac_bridge.cleanup()
end
local B={token=token,values={},reverse=setmetatable({},{__mode='k'}),next=0,events={},dropped=0,connections={}}
B.last=g_clock.millis()
function B.cleanup()for _,c in ipairs(B.connections)do disconnect(c.object,c.callbacks) end;__vbotpac_bridge=nil end
local function ref(value)
 local id=B.reverse[value]
 if not id then B.next=B.next+1;assert(B.next<=8192,'Remote object limit reached');id=B.next;B.values[id]=value;B.reverse[value]=id end
 return {__vbp_ref=id,kind=type(value)}
end
local function encode(v,depth,seen)
 local t=type(v)
 if t=='userdata' or t=='function' then return ref(v) end
 if t~='table' then return v end
 if getmetatable(v) then return ref(v) end
 assert(depth<8,'Remote result nesting limit')
 assert(not seen[v],'Cyclic remote result');seen[v]=true
 local out={};local n=0
 for k,x in pairs(v)do n=n+1;assert(n<=4096,'Remote result entry limit');out[k]=encode(x,depth+1,seen) end
 seen[v]=nil;return out
end
local function decode(v)
 if type(v)~='table' then return v end
 if v.__vbp_ref then return assert(B.values[v.__vbp_ref],'Expired remote object') end
 local out={};for k,x in pairs(v)do out[k]=decode(x) end;return out
end
local function jsonString(s)
 return '"'..s:gsub('[%z\1-\31\\"]',function(c)
  if c=='"' then return '\\"' elseif c=='\\' then return '\\\\' end
  return string.format('\\u%04x',string.byte(c))
 end)..'"'
end
local function jsonValue(v)
 local t=type(v)
 if t=='nil' then return 'null' elseif t=='string' then return jsonString(v)
 elseif t=='number' then assert(v==v and v~=math.huge and v~=-math.huge,'Nonfinite result');return tostring(v)
 elseif t=='boolean' then return tostring(v) end
 assert(t=='table','Unserializable result')
 local out={};local array=#v>0
 for k in pairs(v)do if type(k)~='number' or k<1 or k>#v or k%1~=0 then array=false end end
 if array then for i=1,#v do out[#out+1]=jsonValue(v[i]) end;return '['..table.concat(out,',')..']' end
 for k,x in pairs(v)do out[#out+1]=jsonString(tostring(k))..':'..jsonValue(x) end
 return '{'..table.concat(out,',')..'}'
end
function B.dispatch(request)
 assert(request.token==B.token,'Attachment changed')
 B.last=g_clock.millis()
 local ok,value=pcall(function()
  if request.op=='close' then
   B.cleanup();return true
  end
  if request.op=='events' then local result={events=B.events,dropped=B.dropped};B.events={};B.dropped=0;return result end
  if request.op=='eval' then local f=assert(loadstring(request.source));return f() end
  if request.op=='snapshot' then
   local p=g_game.getLocalPlayer();return {online=g_game.isOnline(),name=p and p:getName(),position=p and p:getPosition(),version=_VERSION}
  end
  local target=request.root and _G[request.root] or B.values[request.ref]
  assert(target~=nil,'Remote target unavailable')
  if request.op=='get' then return target[request.key] end
  if request.op=='call' then
   local args=decode(request.args or {})
   if request.multi then
    local function pack(...)return {__vbp_returns=true,count=select('#',...),values={...}} end
    return pack(target(unpack(args,1,request.count or #args)))
   end
   return target(unpack(args,1,request.count or #args))
  end
  error('Unknown remote operation')
 end)
 return jsonValue(ok and {ok=true,value=encode(value,0,{})} or {ok=false,error=tostring(value)})
end
__vbotpac_bridge=B
local events={
 g_game={onTalk='onTalk',onTextMessage='onTextMessage',onLoginAdvice='onLoginAdvice',onUse='onUse',onUseWith='onUseWith',onChannelList='onChannelList',onOpenChannel='onOpenChannel',onCloseChannel='onCloseChannel',onChannelEvent='onChannelEvent',onImbuementWindow='onImbuementWindow',onModalDialog='onModalDialog',onAttackingCreatureChange='onAttackingCreatureChange',onGameEditText='onGameEditText',onSpellCooldown='onSpellCooldown',onSpellGroupCooldown='onGroupSpellCooldown'},
 Tile={onAddThing='onAddThing',onRemoveThing='onRemoveThing'},
 Creature={onAppear='onCreatureAppear',onDisappear='onCreatureDisappear',onPositionChange='onCreaturePositionChange',onHealthPercentChange='onCreatureHealthPercentChange',onTurn='onTurn',onWalk='onWalk'},
 LocalPlayer={onPositionChange='onCreaturePositionChange',onHealthPercentChange='onCreatureHealthPercentChange',onTurn='onTurn',onWalk='onWalk',onManaChange='onManaChange',onStatesChange='onStatesChange',onInventoryChange='onInventoryChange'},
 Container={onOpen='onContainerOpen',onClose='onContainerClose',onUpdateItem='onContainerUpdateItem',onAddItem='onAddItem',onRemoveItem='onRemoveItem'},
 g_map={onMissle='onMissle',onAnimatedText='onAnimatedText',onStaticText='onStaticText'}}
if connect then for name,mapping in pairs(events)do
 local object=_G[name]
 if object then
  local callbacks={}
  for event,callbackName in pairs(mapping)do local output=callbackName
   callbacks[event]=function(...)
    if g_clock.millis()-B.last>5000 then return end
    if #B.events>=256 then B.dropped=B.dropped+1;return end
    local args={};for i=1,select('#',...)do args[i]=select(i,...) end
    B.events[#B.events+1]={name=output,args=args,count=select('#',...)}
   end
  end
  connect(object,callbacks);B.connections[#B.connections+1]={object=object,callbacks=callbacks}
 end
end end
return 'ready'
