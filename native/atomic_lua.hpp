#pragma once
#include "lua_executor.hpp"
#include <cmath>
#include <cstdint>
#include <cstring>

namespace pinj {
// No loadbuffer member: primitive dispatch cannot compile source.
struct AtomicLuaApi {
    Pcall pcall; Gettop gettop; Settop settop; Tolstring tolstring;
    void (*getfield)(lua_State*,int,const char*);
    int (*type)(lua_State*,int);
    int (*toboolean)(lua_State*,int);
    double (*tonumber)(lua_State*,int);
    void (*pushinteger)(lua_State*,std::intptr_t);
    void (*pushvalue)(lua_State*,int);
    void (*pushcclosure)(lua_State*,int(*)(lua_State*),int);
    int (*checkstack)(lua_State*,int);
    void (*createtable)(lua_State*,int,int);
    void (*setfield)(lua_State*,int,const char*);
    void (*pushboolean)(lua_State*,int);
    void (*pushlstring)(lua_State*,const char*,std::size_t);
    void (*rawget)(lua_State*,int);
};
struct AtomicPosition { int x=0,y=0,z=0; };
struct AtomicObservation {
    bool game_context=false,online=false,has_player=false,dispatched=false;
    AtomicPosition position;
    int step_ms=0;
    char name[129]={},error[257]={};
    const char* code="unsupported";
    int health=-1,max_health=-1,mana=-1,max_mana=-1;
};
struct AtomicInvocation {
    const AtomicLuaApi* api=nullptr;
    AtomicObservation result;
    bool walk=false,transition=false;
    std::uint64_t deadline=0;
    std::uint64_t (*clock)()=nullptr;
    bool (*authorized)()=nullptr;
    int direction=0;
    AtomicPosition expected;
    const char* expected_name=nullptr;
};
inline thread_local AtomicInvocation* atomic_current=nullptr;
inline bool same_position(const AtomicPosition& a,const AtomicPosition& b) {
    return a.x==b.x&&a.y==b.y&&a.z==b.z;
}
inline void atomic_message(AtomicObservation& result,const char* value,std::size_t size) {
    if(!value) return;
    if(size>256)size=256;
    std::memcpy(result.error,value,size);result.error[size]=0;
}
inline bool atomic_call(const AtomicLuaApi& api,lua_State* L,int nargs,int nresults,AtomicObservation& result) {
    if(api.pcall(L,nargs,nresults,0)==0)return true;
    if(api.type(L,-1)==4) {
        std::size_t size=0;const char* error=api.tolstring(L,-1,&size);atomic_message(result,error,size);
    }
    return false;
}
// Callback body uses only trivially destructible locals: a Lua longjmp must not
// cross C++ objects with destructors. All metamethod/game calls are protected by
// the outer pcall. Nothing is retained in the game global/registry tables.
inline int atomic_callback(lua_State* L) {
    auto& invocation=*atomic_current;
    auto& result=invocation.result;
    const auto& api=*invocation.api;
    constexpr int globals=-10002, table=5, function=6, userdata=7;
    api.getfield(L,globals,"g_game");
    if(api.type(L,-1)!=table)return 0;
    const int game=api.gettop(L);
    api.getfield(L,game,"isOnline");
    if(api.type(L,-1)!=function)return 0;
    result.code="client_rejected";
    if(!atomic_call(api,L,0,1,result))return 0;
    if(api.type(L,-1)!=1){result.code="unsupported";return 0;}
    result.online=api.toboolean(L,-1)!=0;
    api.settop(L,game);
    api.getfield(L,game,"getLocalPlayer");
    if(api.type(L,-1)!=function){result.code="unsupported";return 0;}
    result.game_context=true;
    if(!result.online){result.code=invocation.walk?"precondition":"ok";return 0;}
    if(!atomic_call(api,L,0,1,result))return 0;
    if(api.type(L,-1)!=userdata&&api.type(L,-1)!=table){result.code="precondition";return 0;}
    const int player=api.gettop(L);
    api.getfield(L,player,"getName");
    if(api.type(L,-1)!=function){result.code="unsupported";return 0;}
    api.pushvalue(L,player);
    if(!atomic_call(api,L,1,1,result))return 0;
    if(api.type(L,-1)!=4){result.code="unsupported";return 0;}
    std::size_t length=0;const char* name=api.tolstring(L,-1,&length);
    if(!name||length==0||length>128||std::memchr(name,0,length)){result.code="precondition";return 0;}
    std::memcpy(result.name,name,length);result.name[length]=0;
    api.settop(L,player);
    api.getfield(L,player,"getPosition");
    if(api.type(L,-1)!=function){result.code="unsupported";return 0;}
    api.pushvalue(L,player);
    if(!atomic_call(api,L,1,1,result))return 0;
    if(api.type(L,-1)!=table){result.code="precondition";return 0;}
    const int position=api.gettop(L);
    int* fields[]={&result.position.x,&result.position.y,&result.position.z};
    const char* keys[]={"x","y","z"};
    for(int i=0;i<3;++i) {
        api.getfield(L,position,keys[i]);
        const double value=api.tonumber(L,-1);
        if(api.type(L,-1)!=3||!std::isfinite(value)||value<0||value>(i==2?15:65535)||std::floor(value)!=value){result.code="precondition";return 0;}
        *fields[i]=static_cast<int>(value);api.settop(L,position);
    }
    result.has_player=true;api.settop(L,player);
    // Optional telemetry is copied independently. Missing or malformed values
    // remain unavailable; a client rejection is surfaced without another path.
    if(!invocation.walk){
        const char* getters[]={"getHealth","getMaxHealth","getMana","getMaxMana"};
        int* stats[]={&result.health,&result.max_health,&result.mana,&result.max_mana};
        for(int i=0;i<4;++i){
            api.getfield(L,player,getters[i]);
            if(api.type(L,-1)==function){
                if(invocation.authorized&&!invocation.authorized()){result.code="stale_session";return 0;}
                api.pushvalue(L,player);if(!atomic_call(api,L,1,1,result))return 0;
                const double value=api.tonumber(L,-1);
                if(api.type(L,-1)==3&&std::isfinite(value)&&value>=0&&value<=2147483647&&std::floor(value)==value)*stats[i]=static_cast<int>(value);
            }
            api.settop(L,player);
        }
    }
    const char* directions[]={"North","East","South","West","NorthEast","SouthEast","SouthWest","NorthWest"};
    api.getfield(L,globals,directions[invocation.direction]);
    const double enum_value=api.tonumber(L,-1);
    if(api.type(L,-1)!=3||!std::isfinite(enum_value)||std::floor(enum_value)!=enum_value||enum_value<0||enum_value>255){
        result.code=invocation.walk?"unsupported":"ok";return 0;
    }
    const int direction=static_cast<int>(enum_value);api.settop(L,player);
    api.getfield(L,player,"getStepDuration");
    if(api.type(L,-1)==function) {
        api.pushvalue(L,player);api.pushboolean(L,0);api.pushinteger(L,direction);
        if(!atomic_call(api,L,3,1,result))return 0;
        const double value=api.tonumber(L,-1);
        if(api.type(L,-1)==3&&std::isfinite(value)&&value>=1&&value<=60000&&std::floor(value)==value)result.step_ms=static_cast<int>(value);
    }
    api.settop(L,player);
    if(!invocation.walk){result.code="ok";return 0;}
    if(!invocation.expected_name||std::strcmp(result.name,invocation.expected_name)!=0||!same_position(result.position,invocation.expected)){
        result.code="precondition";return 0;
    }
    // Match the existing live adapter's destination and diagonal terrain guards.
    const int dx[]={0,1,0,-1,1,1,-1,-1},dy[]={-1,0,1,0,-1,1,1,-1};
    const int x=result.position.x,y=result.position.y,z=result.position.z;
    const int xs[]={x+dx[invocation.direction],x+dx[invocation.direction],x};
    const int ys[]={y+dy[invocation.direction],y,y+dy[invocation.direction]};
    const int checks=dx[invocation.direction]&&dy[invocation.direction]?3:1;
    for(int i=0;i<checks;++i) {
        if(xs[i]<0||xs[i]>65535||ys[i]<0||ys[i]>65535){result.code="precondition";return 0;}
        api.settop(L,player);api.getfield(L,globals,"g_map");
        if(api.type(L,-1)!=table){result.code="unsupported";return 0;}
        api.getfield(L,-1,"getTile");
        if(api.type(L,-1)!=function){result.code="unsupported";return 0;}
        api.createtable(L,0,3);
        api.pushinteger(L,xs[i]);api.setfield(L,-2,"x");
        api.pushinteger(L,ys[i]);api.setfield(L,-2,"y");
        api.pushinteger(L,z);api.setfield(L,-2,"z");
        if(!atomic_call(api,L,1,1,result))return 0;
        if(api.type(L,-1)!=table&&api.type(L,-1)!=userdata){result.code="precondition";return 0;}
        const int tile=api.gettop(L);
        const char* guards[]={"isWalkable","isPathable"};
        for(int guard_index=0;guard_index<2;++guard_index) {
            const char* guard=guards[guard_index];
            api.getfield(L,tile,guard);
            if(api.type(L,-1)!=function){result.code="unsupported";return 0;}
            api.pushvalue(L,tile);
            if(!atomic_call(api,L,1,1,result))return 0;
            const bool expected=!(invocation.transition&&i==0&&guard_index==1);
            if(api.type(L,-1)!=1||(api.toboolean(L,-1)!=0)!=expected){result.code="precondition";return 0;}
            api.settop(L,tile);
        }
    }
    // Re-resolve identity and origin after all terrain/timing reads. A binding
    // may run callbacks while being queried; those reads do not authorize acting
    // on an earlier player or position.
    api.settop(L,game);api.getfield(L,game,"isOnline");
    if(api.type(L,-1)!=function){result.code="unsupported";return 0;}
    if(!atomic_call(api,L,0,1,result))return 0;
    if(api.type(L,-1)!=1||!api.toboolean(L,-1)){result.code="precondition";return 0;}
    api.settop(L,game);api.getfield(L,game,"getLocalPlayer");
    if(api.type(L,-1)!=function){result.code="unsupported";return 0;}
    if(!atomic_call(api,L,0,1,result))return 0;
    if(api.type(L,-1)!=table&&api.type(L,-1)!=userdata){result.code="precondition";return 0;}
    const int current_player=api.gettop(L);
    api.getfield(L,current_player,"getName");
    if(api.type(L,-1)!=function){result.code="unsupported";return 0;}
    api.pushvalue(L,current_player);
    if(!atomic_call(api,L,1,1,result))return 0;
    if(api.type(L,-1)!=4){result.code="precondition";return 0;}
    std::size_t current_length=0;const char* current_name=api.tolstring(L,-1,&current_length);
    if(!current_name||current_length!=std::strlen(result.name)||std::memcmp(current_name,result.name,current_length)!=0){result.code="precondition";return 0;}
    api.settop(L,current_player);api.getfield(L,current_player,"getPosition");
    if(api.type(L,-1)!=function){result.code="unsupported";return 0;}
    api.pushvalue(L,current_player);
    if(!atomic_call(api,L,1,1,result))return 0;
    if(api.type(L,-1)!=table){result.code="precondition";return 0;}
    const int current_position=api.gettop(L);
    const int expected_values[]={invocation.expected.x,invocation.expected.y,invocation.expected.z};
    for(int i=0;i<3;++i){
        api.getfield(L,current_position,keys[i]);
        if(api.type(L,-1)!=3||api.tonumber(L,-1)!=expected_values[i]){result.code="precondition";return 0;}
        api.settop(L,current_position);
    }
    api.settop(L,game);api.getfield(L,game,"walk");
    if(api.type(L,-1)!=function){result.code="unsupported";return 0;}
    api.pushinteger(L,direction);
    if(invocation.authorized&&!invocation.authorized()){result.code="stale_session";return 0;}
    if(invocation.deadline&&(!invocation.clock||invocation.clock()>=invocation.deadline)){result.code="expired";return 0;}
    // Once invocation begins, an error is uncertain: never automatically resend.
    result.dispatched=true;result.code="unknown";
    if(!atomic_call(api,L,1,0,result))return 0;
    result.code="dispatched";
    return 0;
}
inline AtomicObservation invoke_atomic(const AtomicLuaApi& api,lua_State* L,bool walk=false,int direction=0,
                                      AtomicPosition expected={},const char* name=nullptr,
                                      std::uint64_t deadline=0,std::uint64_t (*clock)()=nullptr,bool (*authorized)()=nullptr,bool transition=false) {
    AtomicInvocation invocation;
    if(atomic_current||direction<0||direction>7){invocation.result.code="internal";return invocation.result;}
    invocation.api=&api;invocation.walk=walk;invocation.direction=direction;invocation.expected=expected;invocation.expected_name=name;invocation.deadline=deadline;invocation.clock=clock;invocation.authorized=authorized;
    invocation.transition=transition;
    const int top=api.gettop(L);
    if(!api.checkstack(L,32)){invocation.result.code="internal";return invocation.result;}
    atomic_current=&invocation;
    // Temporary native C closure only; no source, registry/global, timer or hook.
    int status=0;
    try {api.pushcclosure(L,atomic_callback,0);status=api.pcall(L,0,0,0);}
    catch(...) {atomic_current=nullptr;api.settop(L,top);invocation.result.code=invocation.result.dispatched?"unknown":"internal";return invocation.result;}
    if(status||std::strcmp(invocation.result.code,"client_rejected")==0||std::strcmp(invocation.result.code,"unknown")==0) {
        if(status)invocation.result.code=invocation.result.dispatched?"unknown":"client_rejected";
        if(api.gettop(L)>top&&api.type(L,-1)==4) {
            std::size_t size=0;const char* error=api.tolstring(L,-1,&size);atomic_message(invocation.result,error,size);
        }
    }
    atomic_current=nullptr;api.settop(L,top);
    return invocation.result;
}
}
