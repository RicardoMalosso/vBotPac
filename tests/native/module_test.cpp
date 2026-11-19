#include "../../native/module_loader.hpp"
#include <cstring>
#ifdef _WIN32
#define EXPORT extern "C" __declspec(dllexport)
#else
#define EXPORT extern "C" __attribute__((visibility("default")))
#endif
EXPORT int module_test(const pinj::LuaApi* api,const pinj::AtomicLuaApi* stack,lua_State* L,const char* source,char* output,std::size_t capacity) {
 try {const auto result=pinj::execute_module(*api,*stack,L,source);if(result.size()+1>capacity)return 1;std::memcpy(output,result.c_str(),result.size()+1);return 0;}
 catch(...){return 2;}
}
