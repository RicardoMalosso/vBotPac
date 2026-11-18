#pragma once
#include "atomic_lua.hpp"
#include <array>
namespace pinj {
struct AtomicItem {int id=0,count=1,container=0,slot=0;bool movable=false;};
struct AtomicTile {AtomicPosition position;bool walkable=false,pathable=false;int count=0;std::array<AtomicItem,33> items{};};
struct AtomicItemView {AtomicTile tile;int count=0;bool truncated=false;std::array<AtomicItem,128> inventory{};};
struct AtomicItemsCall {
 const AtomicLuaApi* api;AtomicObservation result;AtomicItemView view;
 const AtomicItemView* expected=nullptr;AtomicPosition origin,position,destination;
 const char* name=nullptr;int mechanism=-1,target=0,tool=0;std::uint64_t deadline=0;
 std::uint64_t (*clock)()=nullptr;bool (*authorized)()=nullptr;
};
inline thread_local AtomicItemsCall* atomic_items_current=nullptr;
inline bool item_object(const AtomicLuaApi& a,lua_State* L,int index){return a.type(L,index)==5||a.type(L,index)==7;}
inline bool item_number(const AtomicLuaApi& a,lua_State* L,int object,const char* getter,int low,int high,int& out,AtomicObservation& r,bool optional=false){
 const int top=a.gettop(L);a.getfield(L,object,getter);
 if(a.type(L,-1)!=6){a.settop(L,top);if(!optional)r.code="unsupported";return optional;}
 a.pushvalue(L,object);if(!atomic_call(a,L,1,1,r))return false;
 const double n=a.tonumber(L,-1);
 if(a.type(L,-1)!=3||!std::isfinite(n)||std::floor(n)!=n||n<low||n>high){r.code="precondition";return false;}
 out=static_cast<int>(n);a.settop(L,top);return true;
}
inline bool copy_item(const AtomicLuaApi& a,lua_State* L,int object,AtomicItem& item,AtomicObservation& r){
 if(!item_object(a,L,object)){r.code="precondition";return false;}
 if(!item_number(a,L,object,"getId",1,65535,item.id,r)||!item_number(a,L,object,"getCount",0,65535,item.count,r,true))return false;
 const int top=a.gettop(L);a.getfield(L,object,"getCount");const bool counted=a.type(L,-1)==6;a.settop(L,top);a.getfield(L,object,"isMoveable");
 if(a.type(L,-1)==6){a.pushvalue(L,object);if(!atomic_call(a,L,1,1,r))return false;item.movable=counted&&a.type(L,-1)==1&&a.toboolean(L,-1);}
 a.settop(L,top);return true;
}
inline void item_position(const AtomicLuaApi& a,lua_State* L,const AtomicPosition& p){
 a.createtable(L,0,3);a.pushinteger(L,p.x);a.setfield(L,-2,"x");a.pushinteger(L,p.y);a.setfield(L,-2,"y");a.pushinteger(L,p.z);a.setfield(L,-2,"z");
}
inline bool read_item_tile(const AtomicLuaApi& a,lua_State* L,const AtomicPosition& p,AtomicTile& tile,AtomicObservation& r,int& ground,int& items,bool ignore_creatures=false){
 const int base=a.gettop(L);a.getfield(L,-10002,"g_map");if(!item_object(a,L,-1)){r.code="unsupported";return false;}
 a.getfield(L,-1,"getTile");if(a.type(L,-1)!=6){r.code="unsupported";return false;}
 item_position(a,L,p);if(!atomic_call(a,L,1,1,r))return false;
 if(!item_object(a,L,-1)){r.code="precondition";return false;}const int t=a.gettop(L);tile.position=p;
 const char* flags[]={"isWalkable","isPathable"};bool* values[]={&tile.walkable,&tile.pathable};
 for(int i=0;i<2;++i){a.getfield(L,t,flags[i]);if(a.type(L,-1)!=6){r.code="unsupported";return false;}a.pushvalue(L,t);if(i==0&&ignore_creatures)a.pushboolean(L,1);if(!atomic_call(a,L,i==0&&ignore_creatures?2:1,1,r))return false;
  if(a.type(L,-1)!=1){r.code="precondition";return false;}*values[i]=a.toboolean(L,-1)!=0;a.settop(L,t);}
 a.getfield(L,t,"getGround");if(a.type(L,-1)!=6){r.code="unsupported";return false;}a.pushvalue(L,t);if(!atomic_call(a,L,1,1,r))return false;ground=a.gettop(L);
 if(item_object(a,L,ground)){auto& item=tile.items[tile.count++];item.slot=0;if(!copy_item(a,L,ground,item,r))return false;}
 else if(a.type(L,ground)!=0){r.code="precondition";return false;}
 a.getfield(L,t,"getItems");if(a.type(L,-1)!=6){r.code="unsupported";return false;}a.pushvalue(L,t);if(!atomic_call(a,L,1,1,r))return false;
 if(a.type(L,-1)!=5){r.code="precondition";return false;}items=a.gettop(L);
 for(int i=1;i<=33;++i){a.pushinteger(L,i);a.rawget(L,items);if(a.type(L,-1)==0){a.settop(L,items);return true;}
  if(i>32||tile.count>=33){r.code="capacity";return false;}auto& item=tile.items[tile.count++];item.slot=i;
  if(!copy_item(a,L,a.gettop(L),item,r))return false;
  a.settop(L,items);
 }
 a.settop(L,base);return false;
}
inline bool same_item(const AtomicItem& a,const AtomicItem& b){return a.id==b.id&&a.count==b.count&&a.slot==b.slot&&a.movable==b.movable;}
inline bool same_tile(const AtomicTile& a,const AtomicTile& b){
 if(!same_position(a.position,b.position)||a.walkable!=b.walkable||a.pathable!=b.pathable||a.count!=b.count)return false;
 for(int i=0;i<a.count;++i)if(!same_item(a.items[i],b.items[i]))return false;
 return true;
}
inline bool read_inventory(const AtomicLuaApi& a,lua_State* L,AtomicItemView& view,AtomicObservation& r){
 const int base=a.gettop(L);a.getfield(L,-10002,"g_game");if(!item_object(a,L,-1)){r.code="unsupported";return false;}const int game=a.gettop(L);
 a.getfield(L,game,"getLocalPlayer");if(a.type(L,-1)!=6){r.code="unsupported";return false;}if(!atomic_call(a,L,0,1,r))return false;const int player=a.gettop(L);
 for(int slot=1;slot<=10;++slot){a.getfield(L,player,"getInventoryItem");if(a.type(L,-1)!=6){view.truncated=true;a.settop(L,player);break;}
  a.pushvalue(L,player);a.pushinteger(L,slot);if(!atomic_call(a,L,2,1,r))return false;
  if(item_object(a,L,-1)){auto& item=view.inventory[view.count++];item.slot=slot;if(!copy_item(a,L,a.gettop(L),item,r))return false;}
  else if(a.type(L,-1)!=0){r.code="precondition";return false;}
  a.settop(L,player);
 }
 a.settop(L,game);a.getfield(L,game,"getContainers");if(a.type(L,-1)!=6){view.truncated=true;a.settop(L,base);return true;}
 if(!atomic_call(a,L,0,1,r))return false;
 if(a.type(L,-1)!=5){r.code="precondition";return false;}const int containers=a.gettop(L);
 for(int id=0;id<32;++id){a.pushinteger(L,id);a.rawget(L,containers);
  if(a.type(L,-1)==0){a.settop(L,containers);continue;}if(!item_object(a,L,-1)){r.code="precondition";return false;}const int container=a.gettop(L);
  a.getfield(L,container,"getItems");if(a.type(L,-1)!=6){view.truncated=true;a.settop(L,containers);continue;}a.pushvalue(L,container);if(!atomic_call(a,L,1,1,r))return false;
  if(a.type(L,-1)!=5){r.code="precondition";return false;}const int items=a.gettop(L);
  for(int slot=1;slot<=257;++slot){a.pushinteger(L,slot);a.rawget(L,items);if(a.type(L,-1)==0)break;
   if(slot>256||view.count>=128){view.truncated=true;a.settop(L,base);return true;}
   auto& item=view.inventory[view.count++];item.container=id+1;item.slot=slot;if(!copy_item(a,L,a.gettop(L),item,r))return false;
  a.settop(L,items);
  }a.settop(L,containers);
 }a.settop(L,base);return true;
}
inline bool resolve_tool(const AtomicLuaApi& a,lua_State* L,const AtomicItem& expected,AtomicObservation& r,int& tool){
 a.getfield(L,-10002,"g_game");if(!item_object(a,L,-1)){r.code="unsupported";return false;}const int game=a.gettop(L);
 a.getfield(L,game,expected.container?"getContainers":"getLocalPlayer");if(a.type(L,-1)!=6){r.code="unsupported";return false;}
 if(!atomic_call(a,L,0,1,r))return false;
 if(expected.container){if(a.type(L,-1)!=5){r.code="precondition";return false;}a.pushinteger(L,expected.container-1);a.rawget(L,a.gettop(L)-1);
  if(!item_object(a,L,-1)){r.code="precondition";return false;}const int container=a.gettop(L);a.getfield(L,container,"getItems");if(a.type(L,-1)!=6){r.code="unsupported";return false;}a.pushvalue(L,container);if(!atomic_call(a,L,1,1,r))return false;
  if(a.type(L,-1)!=5){r.code="precondition";return false;}a.pushinteger(L,expected.slot);a.rawget(L,a.gettop(L)-1);
 }else{if(!item_object(a,L,-1)){r.code="precondition";return false;}const int player=a.gettop(L);a.getfield(L,player,"getInventoryItem");if(a.type(L,-1)!=6){r.code="unsupported";return false;}a.pushvalue(L,player);a.pushinteger(L,expected.slot);if(!atomic_call(a,L,2,1,r))return false;}
 tool=a.gettop(L);AtomicItem item;if(!copy_item(a,L,tool,item,r))return false;
 if(item.id!=expected.id||item.count!=expected.count){r.code="precondition";return false;}return true;
}
inline int atomic_items_callback(lua_State* L){
 auto& c=*atomic_items_current;const auto& a=*c.api;auto& r=c.result;r.code="client_rejected";
 const auto before=invoke_atomic(a,L);if(std::strcmp(before.code,"ok")||!before.has_player){r=before;if(!std::strcmp(r.code,"ok"))r.code="precondition";return 0;}
 if(!c.name||std::strcmp(before.name,c.name)||!same_position(before.position,c.origin)||c.position.z!=c.origin.z||std::abs(c.position.x-c.origin.x)>1||std::abs(c.position.y-c.origin.y)>1){r.code="precondition";return 0;}
 if(c.mechanism<0){if(!read_inventory(a,L,c.view,r))return 0;int ground=0,items=0;if(!read_item_tile(a,L,c.position,c.view.tile,r,ground,items))return 0;r.code="ok";return 0;}
 int tool=0;if(c.mechanism==1||(c.mechanism==3&&c.tool<128)){if(!c.expected||c.tool<0||c.tool>=c.expected->count){r.code="precondition";return 0;}if(!resolve_tool(a,L,c.expected->inventory[c.tool],r,tool))return 0;}
 if(c.mechanism==3){
  if(same_position(c.origin,c.position)){r.code="precondition";return 0;}
  const int top=a.gettop(L);AtomicTile drop;int drop_ground=0,drop_items=0;
  if(!read_item_tile(a,L,c.origin,drop,r,drop_ground,drop_items,true))return 0;
  if(!drop.walkable||!drop.pathable||drop.count>8){r.code="precondition";return 0;}a.settop(L,top);
 }
 int ground=0,items=0;if(!read_item_tile(a,L,c.position,c.view.tile,r,ground,items))return 0;
 if(!c.expected||!same_tile(c.view.tile,c.expected->tile)){r.code="precondition";return 0;}
 int target=0;for(int i=0;i<c.view.tile.count;++i)if(c.view.tile.items[i].slot==c.target)target=i+1;
 if(c.mechanism==2||c.mechanism==4){
  const int dx=c.position.x-c.origin.x,dy=c.position.y-c.origin.y;
  const int xs[]={0,1,0,-1,1,1,-1,-1},ys[]={-1,0,1,0,-1,1,1,-1};int direction=-1;
  for(int i=0;i<8;++i)if(dx==xs[i]&&dy==ys[i])direction=i;
  if(direction<0){r.code="precondition";return 0;}
  r=invoke_atomic(a,L,true,direction,c.origin,c.name,c.deadline,c.clock,c.authorized,c.mechanism==2);return 0;
 }
 if(!target){r.code="precondition";return 0;}
 a.getfield(L,-10002,"g_game");if(!item_object(a,L,-1)){r.code="unsupported";return 0;}const int game=a.gettop(L);
 a.getfield(L,game,c.mechanism==1?"useWith":(c.mechanism==3?"move":"use"));if(a.type(L,-1)!=6){r.code="unsupported";return 0;}
 if(c.mechanism==1)a.pushvalue(L,tool);
 if(c.target==0)a.pushvalue(L,ground);else{a.pushinteger(L,c.target);a.rawget(L,items);}
 if(c.mechanism==3){
  const auto& selected=c.view.tile.items[target-1];int last=0;
  for(int i=0;i<c.view.tile.count;++i)if(c.view.tile.items[i].slot>last)last=c.view.tile.items[i].slot;
  if(c.target==0||c.target!=last||!selected.movable){r.code="precondition";return 0;}
  item_position(a,L,c.origin);a.pushinteger(L,selected.count?selected.count:1);
 }
 // Recheck identity/origin immediately before the one mutating API call.
 const auto current=invoke_atomic(a,L);if(std::strcmp(current.code,"ok")||!current.has_player||std::strcmp(current.name,c.name)||!same_position(current.position,c.origin)){r.code="precondition";return 0;}
 if((c.authorized&&!c.authorized())||(c.deadline&&(!c.clock||c.clock()>=c.deadline))){r.code="expired";return 0;}
 r.dispatched=true;r.code="unknown";if(!atomic_call(a,L,c.mechanism==1?2:(c.mechanism==3?3:1),0,r))return 0;r.code="dispatched";return 0;
}
inline AtomicItemsCall invoke_items(AtomicItemsCall call,lua_State* L){
 const auto& a=*call.api;if(atomic_items_current||!a.rawget)return call;const int top=a.gettop(L);if(!a.checkstack(L,64)){call.result.code="internal";return call;}
 atomic_items_current=&call;
 try{a.pushcclosure(L,atomic_items_callback,0);if(a.pcall(L,0,0,0)){call.result.code=call.result.dispatched?"unknown":"client_rejected";if(a.type(L,-1)==4){std::size_t n=0;const char* e=a.tolstring(L,-1,&n);atomic_message(call.result,e,n);}}}
 catch(...){call.result.code=call.result.dispatched?"unknown":"internal";}
 atomic_items_current=nullptr;a.settop(L,top);return call;
}
}
