-- UI-process facade. Game objects/functions stay in the selected client's VM.
externalRemote={}
local R=externalRemote
local sequence=0
local proxies={}
local ffi=require('ffi')
if ffi.os=='Windows' then ffi.cdef('void Sleep(unsigned long);') else ffi.cdef('int usleep(unsigned int);') end
local function sleep()if ffi.os=='Windows' then ffi.C.Sleep(2) else ffi.C.usleep(2000) end end
local function read(path)local f=io.open(path,'rb');if not f then return end;local s=f:read('*a');f:close();return s end
local function rpc(request)
 assert(not R.failed,'Attachment ended; restart to reconnect')
 sequence=sequence+1;request.seq=sequence;request.token=VBP_TOKEN;request.expires=os.time()+3
 local f=assert(io.open(VBP_RPC_PATH..'/request.tmp','wb'));f:write(json.encode(request));f:close()
 assert(os.rename(VBP_RPC_PATH..'/request.tmp',VBP_RPC_PATH..'/request.json'))
 local untilTime=g_clock.millis()+3000
 repeat
  local text=read(VBP_RPC_PATH..'/response.json')
  if text then
   local ok,response=pcall(json.decode,text)
   if ok and response.seq==sequence and response.token==VBP_TOKEN then
    os.remove(VBP_RPC_PATH..'/response.json')
    if not response.ok then error(response.error or 'Remote call failed') end
    return response.value
   end
  end
  sleep()
 until g_clock.millis()>untilTime
 R.failed=true;error('Client RPC timed out; command outcome unknown; no replay')
end
local decode,encode
local function proxy(target,kind)
 local cacheKey=target.ref and tostring(target.ref) or target.root
 if proxies[cacheKey] then return proxies[cacheKey] end
 local p={};local mt={__vbp_target=target,__vbp_kind=kind}
 function mt.__index(_,key)
  local value=decode(rpc({op='get',root=target.root,ref=target.ref,key=key}))
  if type(value)=='table' and getmetatable(value) and getmetatable(value).__vbp_kind=='function' then rawset(p,key,value) end
  return value
 end
 function mt.__call(_,...)
  local args={};for i=1,select('#',...)do args[i]=encode(select(i,...)) end
  local result=decode(rpc({op='call',root=target.root,ref=target.ref,args=args,count=select('#',...),multi=true}))
  if result and result.__vbp_returns then
   local values={};for i=1,result.count do
    local value=result.values[i];if value==nil then value=result.values[tostring(i)] end;values[i]=value
   end
   return unpack(values,1,result.count)
  end
  return result
 end
 setmetatable(p,mt);proxies[cacheKey]=p;return p
end
encode=function(value)
 if type(value)~='table' then assert(type(value)~='function','Local callbacks cannot be passed to the game');return value end
 local mt=getmetatable(value)
 if mt and mt.__vbp_target then assert(mt.__vbp_target.ref,'Cannot pass a global root');return {__vbp_ref=mt.__vbp_target.ref} end
 local out={};for k,v in pairs(value)do out[k]=encode(v) end;return out
end
decode=function(value)
 if type(value)~='table' then return value end
 if value.__vbp_ref then return proxy({ref=value.__vbp_ref},value.kind) end
 local out={};for k,v in pairs(value)do out[k]=decode(v) end;return out
end
function R.install(context)
 for _,name in ipairs({'g_game','g_map','g_things','Item','Creature','ThingType','Effect','Missile','Player','Monster','StaticText','OutputMessage','Container','LocalPlayer','Tile'})do
  context[name]=proxy({root=name},'table')
 end
 context.player=context.g_game.getLocalPlayer()
 context.clientLua=function(source)return decode(rpc({op='eval',source=source})) end
 context.clientGlobal=function(name)return proxy({root=name},'table') end
 context.type=function(value)local mt=type(value)=='table' and getmetatable(value);return mt and mt.__vbp_kind or type(value) end
end
function R.poll(callbacks)
 local batch=decode(rpc({op='events'}))
 assert(batch.dropped==0,'Client event queue overflow; automation stopped')
 for _,event in ipairs(batch.events or {})do
  local callback=callbacks[event.name]
  if callback then callback(unpack(event.args,1,event.count)) end
 end
end
