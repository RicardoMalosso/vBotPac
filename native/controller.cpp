#include <cstdio>
#include <io.h>
#include <fcntl.h>
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <tlhelp32.h>
#include <iostream>
#include <fstream>
#include <sstream>
#include <filesystem>
#include "profile.hpp"
struct Handle { HANDLE h; ~Handle(){if(h&&h!=INVALID_HANDLE_VALUE)CloseHandle(h);} };
[[noreturn]] void fail(const char* msg){throw std::runtime_error(std::string(msg)+" (Windows error "+std::to_string(GetLastError())+")");}
uintptr_t module(DWORD pid,const std::wstring& name) {
    Handle snap{CreateToolhelp32Snapshot(TH32CS_SNAPMODULE|TH32CS_SNAPMODULE32,pid)};
    MODULEENTRY32W e{};e.dwSize=sizeof(e);
    if(Module32FirstW(snap.h,&e))do{if(_wcsicmp(e.szModule,name.c_str())==0)return reinterpret_cast<uintptr_t>(e.modBaseAddr);}while(Module32NextW(snap.h,&e));
    throw std::runtime_error("Required module not found in target");
}
void inject(DWORD pid,const std::wstring& dll) {
    Handle process{OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION|PROCESS_CREATE_THREAD|PROCESS_VM_OPERATION|PROCESS_VM_WRITE|PROCESS_VM_READ,FALSE,pid)};
    if(!process.h)fail("OpenProcess failed");
    wchar_t exe[32768];DWORD n=32768;
    if(!QueryFullProcessImageNameW(process.h,0,exe,&n))fail("Cannot resolve target path");
    if(hash_file(std::wstring(exe,n))!=fingerprint)throw std::runtime_error("Unsupported executable SHA256");
    USHORT processMachine=0,nativeMachine=0;
    if(!IsWow64Process2(process.h,&processMachine,&nativeMachine)||processMachine!=IMAGE_FILE_MACHINE_UNKNOWN||nativeMachine!=IMAGE_FILE_MACHINE_AMD64)throw std::runtime_error("Target must be native x64");
    auto load=GetProcAddress(GetModuleHandleW(L"kernel32.dll"),"LoadLibraryW");
    HMODULE owner=nullptr;
    if(!load||!GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS|GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,reinterpret_cast<LPCWSTR>(load),&owner))fail("Cannot resolve loader");
    wchar_t ownerPath[32768];if(!GetModuleFileNameW(owner,ownerPath,32768))fail("Cannot resolve loader module");
    auto remoteLoad=module(pid,std::filesystem::path(ownerPath).filename().wstring())+(reinterpret_cast<uintptr_t>(load)-reinterpret_cast<uintptr_t>(owner));
    auto bytes=(dll.size()+1)*sizeof(wchar_t);
    void* remote=VirtualAllocEx(process.h,nullptr,bytes,MEM_COMMIT|MEM_RESERVE,PAGE_READWRITE);
    if(!remote)fail("Remote allocation failed");
    SIZE_T written=0;
    if(!WriteProcessMemory(process.h,remote,dll.c_str(),bytes,&written)||written!=bytes){VirtualFreeEx(process.h,remote,0,MEM_RELEASE);fail("Remote write failed");}
    Handle thread{CreateRemoteThread(process.h,nullptr,0,reinterpret_cast<LPTHREAD_START_ROUTINE>(remoteLoad),remote,0,nullptr)};
    if(!thread.h){VirtualFreeEx(process.h,remote,0,MEM_RELEASE);fail("Loader thread failed");}
    if(WaitForSingleObject(thread.h,10000)!=WAIT_OBJECT_0)throw std::runtime_error("Loader timeout; remote path retained until client exits");
    VirtualFreeEx(process.h,remote,0,MEM_RELEASE);
    // A remote HMODULE is 64-bit; do not interpret the 32-bit thread exit code as a pointer.
    module(pid,std::filesystem::path(dll).filename().wstring());
}
bool transfer(HANDLE pipe,void* data,DWORD size,bool write) {
    auto* p=static_cast<char*>(data);
    while(size){DWORD n=0;BOOL ok=write?WriteFile(pipe,p,size,&n,nullptr):ReadFile(pipe,p,size,&n,nullptr);if(!ok||!n)return false;p+=n;size-=n;}return true;
}
int wmain(int argc,wchar_t** argv) {
 try {
    if(argc<3){std::cerr<<"Usage: pinj <PID> <absolute-bridge.dll>\n";return 2;}
    size_t consumed=0;auto raw=std::stoull(argv[1],&consumed);
    if(consumed!=wcslen(argv[1])||raw==0||raw>MAXDWORD)throw std::runtime_error("Invalid PID");
    DWORD pid=static_cast<DWORD>(raw);
    auto dll=std::filesystem::absolute(argv[2]).wstring();
    if(!std::filesystem::is_regular_file(dll))throw std::runtime_error("DLL file does not exist");
    // Do not load a second copy when the bridge is already connected.
    if(!WaitNamedPipeW(pipe_name(pid).c_str(),100))inject(pid,dll);
    for(int i=0;i<50&&!WaitNamedPipeW(pipe_name(pid).c_str(),100);++i)Sleep(100);
    Handle pipe{CreateFileW(pipe_name(pid).c_str(),GENERIC_READ|GENERIC_WRITE,0,nullptr,OPEN_EXISTING,0,nullptr)};
    if(pipe.h==INVALID_HANDLE_VALUE)fail("Bridge unavailable (check debugger output for profile/hook errors)");
    if(argc==4 && std::wstring(argv[3])==L"--stdio") {
      _setmode(_fileno(stdin),_O_BINARY);_setmode(_fileno(stdout),_O_BINARY);
      for(;;) {
        DWORD length=0;
        if(!std::cin.read(reinterpret_cast<char*>(&length),4))break;
        if(!length||length>65541)throw std::runtime_error("Invalid input frame");
        std::string command(length,'\0');
        if(!std::cin.read(command.data(),length))throw std::runtime_error("Incomplete input frame");
        if(!transfer(pipe.h,&length,4,true)||!transfer(pipe.h,command.data(),length,true))fail("Write failed");
        if(!transfer(pipe.h,&length,4,false)||!length||length>1048576)fail("Invalid response frame");
        std::string response(length,'\0');if(!transfer(pipe.h,response.data(),length,false))fail("Read failed");
        std::cout.write(reinterpret_cast<const char*>(&length),4);std::cout.write(response.data(),length);std::cout.flush();
      }
      return 0;
    }
    std::cerr<<"Connected. status | exec <Lua> | file <path> | dump <output.json> | quit\n";
    std::string line;
    while(std::getline(std::cin,line)){
      if(line=="quit"||line=="disconnect")break;
      std::string command=line, output;
      if(line.rfind("file ",0)==0){std::ifstream f(line.substr(5),std::ios::binary);if(!f){std::cerr<<"Cannot open script\n";continue;}std::ostringstream s;s<<f.rdbuf();command="exec "+s.str();}
      if(line.rfind("dump ",0)==0){output=line.substr(5);command="exec return pinj_console.json(pinj_console.inspect('_G'))";}
      if(command.empty()||command.size()>65541){std::cerr<<"Invalid command size\n";continue;}
      DWORD length=static_cast<DWORD>(command.size());
      if(!transfer(pipe.h,&length,4,true)||!transfer(pipe.h,command.data(),length,true))fail("Write failed");
      if(!transfer(pipe.h,&length,4,false)||length>1048576)fail("Invalid response frame");
      std::string response(length,'\0');if(!transfer(pipe.h,response.data(),length,false))fail("Read failed");
      if(output.empty())std::cout<<response<<'\n';else{if(response.rfind("OK\n",0)!=0){std::cerr<<response<<'\n';continue;}std::ofstream f(output,std::ios::binary);f<<response.substr(3);if(!f)throw std::runtime_error("Cannot save dump");std::cerr<<"Saved response to "<<output<<'\n';}
    }
    return 0;
 }catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}
}
