#pragma once
#include "atomic_lua.hpp"
namespace pinj {
struct AtomicMapResult {bool exporter=false,write_dir=false;char directory[4097]={},error[257]={};const char* name="";const char* code="unsupported";};
struct AtomicMapCall {const AtomicLuaApi* api;AtomicMapResult result;const char* basename;const char* player_name;std::uint64_t deadline;std::uint64_t (*clock)();bool (*authorized)();};
inline thread_local AtomicMapCall* atomic_map_current=nullptr;
inline bool atomic_map_call(const AtomicLuaApi& a,lua_State* L,int n,int results,AtomicMapResult& r){
 if(a.pcall(L,n,results,0)==0)return true;
 if(a.type(L,-1)==4){std::size_t size=0;const char* error=a.tolstring(L,-1,&size);if(error){if(size>256)size=256;std::memcpy(r.error,error,size);r.error[size]=0;}}
 return false;
}
inline int atomic_map_callback(lua_State* L){
 auto& c=*atomic_map_current;auto& r=c.result;const auto& a=*c.api;constexpr int g=-10002;
 const char* names[]={"g_minimapManager","g_minimap"};
 for(const char* name:names){a.getfield(L,g,name);if(a.type(L,-1)==5||a.type(L,-1)==7){a.getfield(L,-1,"saveOtmm");if(a.type(L,-1)==6){r.exporter=true;r.name=name;break;}}a.settop(L,0);}
 a.settop(L,0);a.getfield(L,g,"g_resources");
 if(a.type(L,-1)==5||a.type(L,-1)==7){a.getfield(L,-1,"getWriteDir");r.write_dir=a.type(L,-1)==6;}
 if(!c.basename){r.code="ok";return 0;}
 if(!r.exporter||!r.write_dir||!c.player_name)return 0;
 // The host requested an explicit export, not an arbitrary file write.
 a.settop(L,0);a.getfield(L,g,"g_game");if(a.type(L,-1)!=5)return 0;
 a.getfield(L,-1,"isOnline");if(a.type(L,-1)!=6)return 0;r.code="client_rejected";
 if(!atomic_map_call(a,L,0,1,r))return 0;
 if(a.type(L,-1)!=1||!a.toboolean(L,-1)){r.code="precondition";return 0;}
 a.settop(L,1);a.getfield(L,1,"getLocalPlayer");if(a.type(L,-1)!=6){r.code="unsupported";return 0;}
 if(!atomic_map_call(a,L,0,1,r))return 0;
 if(a.type(L,-1)!=5&&a.type(L,-1)!=7){r.code="precondition";return 0;}
 a.getfield(L,2,"getName");if(a.type(L,-1)!=6){r.code="unsupported";return 0;}a.pushvalue(L,2);
 if(!atomic_map_call(a,L,1,1,r))return 0;
 std::size_t size=0;const char* name=a.tolstring(L,-1,&size);
 if(a.type(L,-1)!=4||!name||size!=std::strlen(c.player_name)||std::memcmp(name,c.player_name,size)){r.code="precondition";return 0;}
 a.settop(L,0);a.getfield(L,g,"g_resources");a.getfield(L,1,"getWriteDir");
 if(!atomic_map_call(a,L,0,1,r))return 0;
 const char* dir=a.tolstring(L,-1,&size);
 if(a.type(L,-1)!=4||!dir||!size||size>4096||std::memchr(dir,0,size)){r.code="precondition";return 0;}
 std::memcpy(r.directory,dir,size);r.directory[size]=0;
 a.settop(L,1);a.getfield(L,1,"fileExists");
 if(a.type(L,-1)==6){
  char path[128]={'/'};std::strncpy(path+1,c.basename,126);a.pushlstring(L,path,std::strlen(path));
  if(!atomic_map_call(a,L,1,1,r))return 0;
  if(a.toboolean(L,-1)){r.code="precondition";return 0;}
 }
 a.settop(L,0);a.getfield(L,g,r.name);a.getfield(L,1,"saveOtmm");
 if(a.type(L,-1)!=6){r.code="unsupported";return 0;}
 char path[128]={'/'};std::strncpy(path+1,c.basename,126);a.pushlstring(L,path,std::strlen(path));
 if((c.authorized&&!c.authorized())||(c.deadline&&(!c.clock||c.clock()>=c.deadline))){r.code="expired";return 0;}
 r.code="unknown";if(!atomic_map_call(a,L,1,1,r))return 0;
 if(a.type(L,-1)==1&&!a.toboolean(L,-1)){r.code="client_rejected";return 0;}
 r.code="ok";return 0;
}
inline AtomicMapResult invoke_atomic_map(const AtomicLuaApi& api,lua_State* L,const char* basename=nullptr,const char* name=nullptr,std::uint64_t deadline=0,std::uint64_t (*clock)()=nullptr,bool (*authorized)()=nullptr){
 AtomicMapCall c{&api,{},basename,name,deadline,clock,authorized};
 if(!api.pushlstring||atomic_map_current)return c.result;
 const int top=api.gettop(L);if(!api.checkstack(L,32))return c.result;
 atomic_map_current=&c;
 try{api.pushcclosure(L,atomic_map_callback,0);const int status=api.pcall(L,0,0,0);
  if(status&&api.type(L,-1)==4){std::size_t size=0;const char* message=api.tolstring(L,-1,&size);if(message){if(size>256)size=256;std::memcpy(c.result.error,message,size);}}
  if(status)c.result.code=std::strcmp(c.result.code,"unknown")==0?"unknown":"client_rejected";
 }catch(...){c.result.code="internal";}
 atomic_map_current=nullptr;api.settop(L,top);return c.result;
}
}
