#pragma once
#include <cstddef>
#include <string>

struct lua_State;
namespace pinj {
using Pcall = int(*)(lua_State*,int,int,int);
using Load = int(*)(lua_State*,const char*,std::size_t,const char*);
using Gettop = int(*)(lua_State*);
using Settop = void(*)(lua_State*,int);
using Tolstring = const char*(*)(lua_State*,int,std::size_t*);
struct LuaApi { Load load; Pcall pcall; Gettop gettop; Settop settop; Tolstring tolstring; };

// Must be called by the owner of L. This helper does not establish thread ownership.
inline std::string execute_lua(const LuaApi& api, lua_State* L, const std::string& source) {
    const int top = api.gettop(L);
    struct Restore {
        const LuaApi& api; lua_State* state; int top;
        ~Restore() { api.settop(state,top); }
    } restore{api,L,top};
    int status = api.load(L,source.data(),source.size(),"=PInj");
    if(status==0) status = api.pcall(L,0,1,0);
    std::size_t length=0;
    const char* result=api.tolstring(L,-1,&length);
    const std::size_t prefix_size=status==0?3:6;
    if(length>1048576-prefix_size) return "ERROR\nLua result exceeds 1 MiB";
    return std::string(status==0?"OK\n":"ERROR\n")+
        (result?std::string(result,length):"<non-string Lua result>");
}
} // namespace pinj
