#pragma once
#include "atomic_lua.hpp"
#include <algorithm>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <thread>
#include <vector>
#ifdef _WIN32
#include <windows.h>
#endif

namespace pinj {
struct ModuleFilesContext {
    LuaApi lua{};
    const AtomicLuaApi* stack=nullptr;
    lua_State* owner=nullptr;
    std::thread::id thread;
};
inline ModuleFilesContext module_files_context;
inline std::string module_file_string(lua_State* L,int index) {
    const auto& c=module_files_context;
    if(c.stack->type(L,index)!=4)throw std::runtime_error("Private file argument must be a string");
    std::size_t size=0;const char* value=c.lua.tolstring(L,index,&size);
    return std::string(value,size);
}
inline std::filesystem::path module_file_path(const std::filesystem::path& root,const std::string& value) {
    if(value.size()>32767||value.find('\0')!=std::string::npos||value.find(':')!=std::string::npos||value.find('\\')!=std::string::npos)
        throw std::runtime_error("Invalid private resource path");
    const auto relative=std::filesystem::u8path(value).relative_path();
    for(const auto& part:relative)if(part=="..")throw std::runtime_error("Private resource path escapes its root");
    const auto resolved=std::filesystem::weakly_canonical(root/relative);
    const auto suffix=resolved.lexically_relative(root);
    if(suffix.empty()||*suffix.begin()=="..")throw std::runtime_error("Private resource path escapes its root");
    return resolved;
}
inline int module_file_failure(lua_State* L,const char* message) {
    const auto& a=*module_files_context.stack;
    a.pushboolean(L,0);a.pushlstring(L,message,std::char_traits<char>::length(message));return 2;
}
inline int module_file_operation(lua_State* L) {
    auto& c=module_files_context;
    if(std::this_thread::get_id()!=c.thread)return 0;
    if(L!=c.owner)return module_file_failure(L,"Private files called outside the owning Lua state");
    try {
        const auto root=std::filesystem::u8path(module_file_string(L,-10003));
        const auto op=module_file_string(L,1),name=module_file_string(L,2);
        const auto target=module_file_path(root,name);
        const auto& a=*c.stack;
        if(op=="exists"||op=="directory") {
            a.pushboolean(L,op=="exists"?std::filesystem::is_regular_file(target):std::filesystem::is_directory(target));return 1;
        }
        if(op=="read") {
            if(!std::filesystem::is_regular_file(target))throw std::runtime_error("Private resource does not exist: "+name);
            const auto size=std::filesystem::file_size(target);
            if(size>128u*1024u*1024u)throw std::runtime_error("Private resource exceeds 128 MiB");
            std::ifstream input(target,std::ios::binary);
            if(!input)throw std::runtime_error("Cannot open private resource: "+name);
            std::string data(static_cast<std::size_t>(size),'\0');
            if(size&&!input.read(data.data(),static_cast<std::streamsize>(size)))throw std::runtime_error("Cannot read private resource: "+name);
            a.pushlstring(L,data.data(),data.size());return 1;
        }
        if(op=="list") {
            std::vector<std::string> entries;
            for(const auto& entry:std::filesystem::directory_iterator(target))entries.push_back(entry.path().filename().u8string());
            std::sort(entries.begin(),entries.end());std::string result;
            for(const auto& entry:entries){result+=entry;result+='\0';}
            a.pushlstring(L,result.data(),result.size());return 1;
        }
        if(op=="mkdir")std::filesystem::create_directories(target);
        else if(op=="remove") {
            if(target==root)throw std::runtime_error("Cannot remove the private resource root");
            std::filesystem::remove_all(target);
        } else if(op=="write"||op=="append") {
            if(target==root)throw std::runtime_error("Cannot write the private resource root");
            const auto data=module_file_string(L,3);
            if(data.size()>128u*1024u*1024u)throw std::runtime_error("Private resource exceeds 128 MiB");
            std::filesystem::create_directories(target.parent_path());
            if(op=="append") {
                std::ofstream output(target,std::ios::binary|std::ios::app);
                output.write(data.data(),static_cast<std::streamsize>(data.size()));output.flush();
                if(!output)throw std::runtime_error("Cannot append private resource: "+name);
            } else {
                auto temporary=target;temporary+=".vbp-tmp";
                if(std::filesystem::is_symlink(temporary))throw std::runtime_error("Symlink temporary resource unsupported");
                {std::ofstream output(temporary,std::ios::binary|std::ios::trunc);
                 output.write(data.data(),static_cast<std::streamsize>(data.size()));output.flush();
                 if(!output)throw std::runtime_error("Cannot write private resource: "+name);}
#ifdef _WIN32
                if(!MoveFileExW(temporary.c_str(),target.c_str(),MOVEFILE_REPLACE_EXISTING|MOVEFILE_WRITE_THROUGH))
                    throw std::runtime_error("Cannot replace private resource: "+name);
#else
                std::filesystem::rename(temporary,target);
#endif
            }
        } else throw std::runtime_error("Unknown private file operation");
        a.pushboolean(L,1);return 1;
    } catch(const std::exception& error) {return module_file_failure(L,error.what());}
}
inline int module_open_files(lua_State* L) {
    auto& c=module_files_context;
    if(std::this_thread::get_id()!=c.thread)return 0;
    if(L!=c.owner)return module_file_failure(L,"Private files opened outside the owning Lua state");
    try {
        const auto value=module_file_string(L,1);
        if(value.empty()||value.size()>32767||value.find('\0')!=std::string::npos)throw std::runtime_error("Invalid private resource root");
        const auto path=std::filesystem::u8path(value);
        if(!path.is_absolute()||!std::filesystem::is_directory(path))throw std::runtime_error("Private resource root must be an existing absolute directory");
        const auto root=std::filesystem::canonical(path).u8string();
        c.stack->pushlstring(L,root.data(),root.size());c.stack->pushcclosure(L,module_file_operation,1);return 1;
    } catch(const std::exception& error) {return module_file_failure(L,error.what());}
}
}
