#pragma once
#include "lua_executor.hpp"
#include "atomic_lua.hpp"
#include "module_files.hpp"
#include <thread>

namespace pinj {
// A private compiler closure for an explicitly bound in-client runtime. It is
// never installed as a replacement for any client global load/loadstring.
struct ModuleCompiler {
    LuaApi lua{};
    const AtomicLuaApi* stack=nullptr;
    lua_State* owner=nullptr;
    std::thread::id thread;
};
inline ModuleCompiler module_compiler;
inline int module_compile(lua_State* L) {
    auto& c=module_compiler;
    if(std::this_thread::get_id()!=c.thread)return 0; // Never touch Lua from another OS thread.
    const auto& a=*c.stack;
    const char* error=nullptr;
    if(L!=c.owner)error="Module compiler called outside its owning Lua state";
    else if(a.type(L,1)!=4)error="Lua source must be a string";
    std::size_t size=0;const char* source=error?nullptr:c.lua.tolstring(L,1,&size);
    if(!error&&size>1048576)error="Module source exceeds 1 MiB";
    if(error){a.pushboolean(L,0);a.pushlstring(L,error,std::char_traits<char>::length(error));return 2;}
    const char* name="=vBotPac";
    if(a.type(L,2)==4)name=c.lua.tolstring(L,2,nullptr);
    if(c.lua.load(L,source,size,name)==0)return 1;
    // load left an error on top. Return false,error without losing the text.
    a.pushboolean(L,0);a.pushvalue(L,-2);return 2;
}
inline std::string execute_module(const LuaApi& lua,const AtomicLuaApi& stack,lua_State* L,const std::string& source) {
    if(!stack.pushcclosure||!stack.pushlstring||!stack.pushboolean)return "ERROR\nNative module compiler unavailable for this profile";
    const int top=lua.gettop(L);
    struct Restore { const LuaApi& a;lua_State* L;int top;~Restore(){a.settop(L,top);} } restore{lua,L,top};
    module_compiler={lua,&stack,L,std::this_thread::get_id()};
    module_files_context={lua,&stack,L,std::this_thread::get_id()};
    int status=lua.load(L,source.data(),source.size(),"=vBotPac.bootstrap");
    if(status==0){stack.pushcclosure(L,module_compile,0);stack.pushcclosure(L,module_open_files,0);status=lua.pcall(L,2,1,0);}
    std::size_t length=0;const char* value=lua.tolstring(L,-1,&length);
    if(length>1048570)return "ERROR\nModule result exceeds frame budget";
    return std::string(status?"ERROR\n":"OK\n")+(value?std::string(value,length):"<non-string Lua result>");
}
}
