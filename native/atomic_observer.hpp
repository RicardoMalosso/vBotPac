#pragma once
#include "atomic_lua.hpp"
#include <array>
#include <atomic>
#include <thread>
namespace pinj {
struct PositionEvent {std::uint64_t seq=0,at=0;AtomicPosition position,previous;};
struct PositionObserver {
 const AtomicLuaApi* api=nullptr;lua_State* owner=nullptr;
 std::thread::id owner_thread;
 std::atomic<bool> active{false},cleanup{false};
 std::atomic<std::uint64_t> lease{0};
 std::uint64_t (*clock)()=nullptr;bool (*authorized)()=nullptr;
 std::array<PositionEvent,128> events{};std::size_t head=0,count=0;
 std::uint64_t sequence=0,dropped=0,generation=0;
 void deactivate(){active=false;cleanup=true;}
};
inline PositionObserver position_observer;
inline bool observer_position(lua_State* L,const AtomicLuaApi& a,int index,AtomicPosition& p){
 if(a.type(L,index)!=5)return false;
 const int top=a.gettop(L);const char* fields[]={"x","y","z"};int* values[]={&p.x,&p.y,&p.z};
 for(int i=0;i<3;++i){a.pushlstring(L,fields[i],1);a.rawget(L,index);const double value=a.tonumber(L,-1);
  if(a.type(L,-1)!=3||!std::isfinite(value)||value<0||value>(i==2?15:65535)||std::floor(value)!=value){a.settop(L,top);return false;}
  *values[i]=static_cast<int>(value);a.settop(L,top);
 }
 return true;
}
// Event callback only copies two positions into a fixed ring. No decisions,
// user scripts, timers, pathfinding, or command dispatch occur here.
inline int observer_position_callback(lua_State* L){
 auto& o=position_observer;if(!o.active)return 0;
 if(o.owner!=L||o.owner_thread!=std::this_thread::get_id()){o.deactivate();return 0;}
 if(o.api->type(L,-10003)!=3||o.api->tonumber(L,-10003)!=static_cast<double>(o.generation))return 0;
 if(o.owner!=L||(o.authorized&&!o.authorized())||!o.clock||o.clock()-o.lease.load()>=10000){o.deactivate();return 0;}
 PositionEvent event;event.seq=++o.sequence;event.at=o.clock();
 if(!observer_position(L,*o.api,2,event.position)||!observer_position(L,*o.api,3,event.previous)){++o.dropped;return 0;}
 if(o.count==o.events.size()){o.head=(o.head+1)%o.events.size();--o.count;++o.dropped;}
 o.events[(o.head+o.count)%o.events.size()]=event;++o.count;return 0;
}
struct ObserverCall {const AtomicLuaApi* api;bool start;const char* code="unsupported";char error[257]={};};
inline thread_local ObserverCall* observer_current=nullptr;
inline bool observer_call(lua_State* L,const AtomicLuaApi& a,int n,int results,ObserverCall& c){
 if(a.pcall(L,n,results,0)==0)return true;
 c.code="client_rejected";if(a.type(L,-1)==4){std::size_t size=0;const char* p=a.tolstring(L,-1,&size);if(p){if(size>256)size=256;std::memcpy(c.error,p,size);}}return false;
}
inline int observer_control_callback(lua_State* L){
 auto& c=*observer_current;const auto& a=*c.api;auto& o=position_observer;constexpr int g=-10002,registry=-10000;
 // The registry owns only the exact target/callback table needed for disconnect.
 a.getfield(L,registry,"pinj.atomic.positions.v1");
 if(a.type(L,-1)==5){
  a.getfield(L,g,"disconnect");if(a.type(L,-1)!=6)return 0;
  a.getfield(L,1,"target");a.getfield(L,1,"callbacks");
  if(!observer_call(L,a,2,0,c))return 0;
  a.pushboolean(L,0);a.setfield(L,registry,"pinj.atomic.positions.v1");
 }
 a.settop(L,0);
 if(!c.start){o.owner=nullptr;o.cleanup=false;c.code="ok";return 0;}
 a.getfield(L,g,"LocalPlayer");if(a.type(L,-1)!=5&&a.type(L,-1)!=7)return 0;
 a.getfield(L,g,"connect");if(a.type(L,-1)!=6)return 0;
 a.getfield(L,g,"disconnect");if(a.type(L,-1)!=6)return 0;
 a.settop(L,1);a.createtable(L,0,1);a.pushinteger(L,static_cast<std::intptr_t>(++o.generation));a.pushcclosure(L,observer_position_callback,1);a.setfield(L,2,"onPositionChange");
 a.createtable(L,0,2);a.pushvalue(L,1);a.setfield(L,3,"target");a.pushvalue(L,2);a.setfield(L,3,"callbacks");
 a.pushvalue(L,3);a.setfield(L,registry,"pinj.atomic.positions.v1");
 a.getfield(L,g,"connect");a.pushvalue(L,1);a.pushvalue(L,2);
 if(!observer_call(L,a,2,0,c)){o.cleanup=true;return 0;}
 o.owner=L;o.api=&a;o.head=o.count=0;o.sequence=o.dropped=0;o.cleanup=false;o.active=true;c.code="ok";return 0;
}
inline ObserverCall invoke_observer(const AtomicLuaApi& a,lua_State* L,bool start,std::uint64_t (*clock)()=nullptr,bool (*authorized)()=nullptr){
 ObserverCall c{&a,start};auto& o=position_observer;
 if(observer_current||(o.owner&&(o.owner!=L||o.owner_thread!=std::this_thread::get_id()))||(start&&(!a.pushlstring||!a.rawget)))return c;
 o.active=false;o.cleanup=false;if(start){o.owner=L;o.owner_thread=std::this_thread::get_id();}
 if(start){o.clock=clock;o.authorized=authorized;o.lease=clock?clock():0;}
 const int top=a.gettop(L);if(!a.checkstack(L,32))return c;
 observer_current=&c;
 try{a.pushcclosure(L,observer_control_callback,0);const int status=a.pcall(L,0,0,0);
  if(status){c.code="client_rejected";if(a.type(L,-1)==4){std::size_t size=0;const char* p=a.tolstring(L,-1,&size);if(p){if(size>256)size=256;std::memcpy(c.error,p,size);}}}
 }catch(...){c.code="internal";}
 observer_current=nullptr;a.settop(L,top);return c;
}
}
