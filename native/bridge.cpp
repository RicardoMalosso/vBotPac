#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <sddl.h>
#include <MinHook.h>
#include <atomic>
#include <memory>
#include <mutex>
#include <string>
#include <vector>
#include <cstring>
#include "profile.hpp"
#include "lua_executor.hpp"
#include "session_gate.hpp"
#include "atomic_protocol.hpp"
#include "atomic_wire.hpp"
#include "pipe_io.hpp"
struct lua_State;
using Pcall=int(*)(lua_State*,int,int,int);
using Load=int(*)(lua_State*,const char*,size_t,const char*);
using Gettop=int(*)(lua_State*);
using Settop=void(*)(lua_State*,int);
using Tolstring=const char*(*)(lua_State*,int,size_t*);
Pcall original=nullptr;Load load=nullptr;Gettop gettop=nullptr;Settop settop=nullptr;Tolstring tolstring=nullptr;
std::atomic<unsigned long long> calls{0};
struct Request {
    std::string source,response,token;
    bool binding=false;
    std::shared_ptr<pinj::AtomicRequest> primitive;
    HANDLE done=CreateEventW(nullptr,TRUE,FALSE,nullptr);
    bool started=false,cancelled=false;
    ~Request(){if(done)CloseHandle(done);}
};
std::mutex pending_mutex;
std::shared_ptr<Request> pending;
pinj::SessionGate gate;
pinj::AtomicLuaApi atomic_api{};
pinj::AtomicSession atomic_session;
std::uint64_t atomic_clock(){return GetTickCount64();}
std::atomic<HANDLE> atomic_pipe{INVALID_HANDLE_VALUE};
bool atomic_authorized(){const HANDLE pipe=atomic_pipe.load();return pipe!=INVALID_HANDLE_VALUE&&PeekNamedPipe(pipe,nullptr,0,nullptr,nullptr,nullptr)!=FALSE;}
thread_local unsigned nesting=0;
int hook(lua_State* L,int nargs,int results,int errfunc) {
    ++nesting;
    int result=original(L,nargs,results,errfunc);
    ++calls;
    if(nesting==1) {
        // Disconnect sets cleanup on the pipe worker. Do not probe IPC on every
        // Lua call; action/event boundaries still check current pipe authority.
        if(pinj::position_observer.owner==L&&(pinj::position_observer.cleanup.load()||GetTickCount64()-pinj::position_observer.lease.load()>=10000)){
            pinj::position_observer.active=false;pinj::invoke_observer(atomic_api,L,false,atomic_clock,atomic_authorized);
        }
        std::shared_ptr<Request> job;
        const auto state=reinterpret_cast<std::uintptr_t>(L);
        const auto thread=GetCurrentThreadId();
        {std::lock_guard<std::mutex> lock(pending_mutex);
         if(pending&&!pending->cancelled&&!pending->started &&
            (pending->binding || pending->token.empty() || gate.matches(pending->token,state,thread,GetTickCount64()))) {
             job=pending;job->started=true;
         }}
        if(job) {
            const pinj::LuaApi api{load,original,gettop,settop,tolstring};
            std::string response;
            if(job->binding) {
                // Probe only known read APIs. A non-game Lua state is skipped.
                {
                    const auto probe=pinj::invoke_atomic(atomic_api,L);
                    response=std::strcmp(probe.code,"client_rejected")==0?"{\"version\":1,\"error\":\"client_rejected\",\"error_message\":"+pinj::atomic_quote(probe.error)+"}":(probe.game_context?"OK\ngame-context":"OK\nother-context");
                }
                std::lock_guard<std::mutex> lock(pending_mutex);
                if(response=="OK\nother-context"&&!job->cancelled) {job->started=false;job.reset();}
                else if(job->cancelled||!atomic_authorized()) response="ERROR\nBinding cancelled";
                else if(response=="OK\ngame-context") {const bool already=gate.owns(job->token,GetTickCount64());
                if(gate.bind(job->token,state,thread,GetTickCount64())) {
                    if(!already)atomic_session.reset(job->token);
                    response="{\"version\":1,\"status\":\"bound\",\"generation\":1,\"monotonic_ms\":"+std::to_string(GetTickCount64())+"}";
                }
                else response="ERROR\nA different external session is already bound";
                }
            } else if(job->primitive) {
                std::lock_guard<std::mutex> lock(pending_mutex);
                response=atomic_authorized()&&gate.matches(job->token,state,thread,GetTickCount64())?
                    atomic_session.dispatch(*job->primitive,atomic_api,L,GetTickCount64(),atomic_clock,atomic_authorized):pinj::atomic_error("stale_session");
            } else response=pinj::execute_lua(api,L,job->source);
            if(job) {
                {std::lock_guard<std::mutex> lock(pending_mutex);job->response=std::move(response);if(pending==job)pending.reset();}
                SetEvent(job->done);
            }
        }
    }
    --nesting;
    return result;
}
std::string execute(const std::string& source,const std::string& token="",bool binding=false,std::shared_ptr<pinj::AtomicRequest> primitive={}) {
    auto job=std::make_shared<Request>();
    if(!job->done)return "ERROR\nCannot allocate request event";
    job->source=source;job->token=token;job->binding=binding;job->primitive=primitive;
    {std::lock_guard<std::mutex> lock(pending_mutex);if(pending)return "ERROR\nQueue busy";
     if(!token.empty()&&!binding&&!gate.renew(token,GetTickCount64()))return primitive?pinj::atomic_error("unbound"):"ERROR\nExternal session is not bound";
     if(primitive){const auto error=atomic_session.reserve(*primitive,GetTickCount64());if(!error.empty())return pinj::atomic_error(error.c_str());}
     pending=job;}
    DWORD wait=WaitForSingleObject(job->done,10000);
    std::lock_guard<std::mutex> lock(pending_mutex);
    if(wait!=WAIT_OBJECT_0){job->cancelled=true;if(pending==job&&!job->started)pending.reset();return job->started?"ERROR\nExecution still running; restart client if script does not return":"ERROR\nNo Lua call observed within 10 seconds; queued command cancelled";}
    return job->response;
}
PSECURITY_DESCRIPTOR pipe_security() {
    HANDLE token=nullptr;
    if(!OpenProcessToken(GetCurrentProcess(),TOKEN_QUERY,&token))return nullptr;
    DWORD size=0;GetTokenInformation(token,TokenUser,nullptr,0,&size);
    std::vector<unsigned char> storage(size);
    if(!GetTokenInformation(token,TokenUser,storage.data(),size,&size)){CloseHandle(token);return nullptr;}
    CloseHandle(token);LPWSTR sid=nullptr;
    if(!ConvertSidToStringSidW(reinterpret_cast<TOKEN_USER*>(storage.data())->User.Sid,&sid))return nullptr;
    std::wstring sddl=L"D:P(A;;GA;;;"+std::wstring(sid)+L")";LocalFree(sid);
    PSECURITY_DESCRIPTOR sd=nullptr;
    if(!ConvertStringSecurityDescriptorToSecurityDescriptorW(sddl.c_str(),SDDL_REVISION_1,&sd,nullptr))return nullptr;
    return sd;
}
bool verify_bytes(uintptr_t base,uintptr_t rva,const unsigned char* bytes,size_t size) {
    return std::memcmp(reinterpret_cast<void*>(base+rva),bytes,size)==0;
}
DWORD WINAPI worker(void*) {
 try {
    wchar_t path[32768];DWORD n=GetModuleFileNameW(nullptr,path,32768);
    if(!n||n==32768||hash_file(std::wstring(path,n))!=fingerprint)throw std::runtime_error("Unsupported executable SHA256");
    auto base=reinterpret_cast<uintptr_t>(GetModuleHandleW(nullptr));
    if(!verify_bytes(base,pcall_rva,pcall_bytes,sizeof(pcall_bytes))
       ||!verify_bytes(base,loadbuffer_rva,loadbuffer_bytes,sizeof(loadbuffer_bytes))
       ||!verify_bytes(base,gettop_rva,gettop_bytes,sizeof(gettop_bytes))
       ||!verify_bytes(base,settop_rva,settop_bytes,sizeof(settop_bytes))
       ||!verify_bytes(base,tolstring_rva,tolstring_bytes,sizeof(tolstring_bytes)))
        throw std::runtime_error("In-memory profile mismatch");
    load=reinterpret_cast<Load>(base+loadbuffer_rva);gettop=reinterpret_cast<Gettop>(base+gettop_rva);
    settop=reinterpret_cast<Settop>(base+settop_rva);tolstring=reinterpret_cast<Tolstring>(base+tolstring_rva);
#ifdef PINJ_ATOMIC_API
    if(!verify_bytes(base,getfield_rva,getfield_bytes,sizeof(getfield_bytes))
       ||!verify_bytes(base,type_rva,type_bytes,sizeof(type_bytes))
       ||!verify_bytes(base,toboolean_rva,toboolean_bytes,sizeof(toboolean_bytes))
       ||!verify_bytes(base,tonumber_rva,tonumber_bytes,sizeof(tonumber_bytes))
       ||!verify_bytes(base,pushinteger_rva,pushinteger_bytes,sizeof(pushinteger_bytes))
       ||!verify_bytes(base,pushvalue_rva,pushvalue_bytes,sizeof(pushvalue_bytes))
       ||!verify_bytes(base,pushcclosure_rva,pushcclosure_bytes,sizeof(pushcclosure_bytes))
       ||!verify_bytes(base,checkstack_rva,checkstack_bytes,sizeof(checkstack_bytes))
       ||!verify_bytes(base,createtable_rva,createtable_bytes,sizeof(createtable_bytes))
       ||!verify_bytes(base,setfield_rva,setfield_bytes,sizeof(setfield_bytes))
       ||!verify_bytes(base,pushboolean_rva,pushboolean_bytes,sizeof(pushboolean_bytes)))throw std::runtime_error("In-memory atomic API profile mismatch");
    atomic_api={original,gettop,settop,tolstring};
    atomic_api.getfield=reinterpret_cast<decltype(atomic_api.getfield)>(base+getfield_rva);
    atomic_api.type=reinterpret_cast<decltype(atomic_api.type)>(base+type_rva);
    atomic_api.toboolean=reinterpret_cast<decltype(atomic_api.toboolean)>(base+toboolean_rva);
    atomic_api.tonumber=reinterpret_cast<decltype(atomic_api.tonumber)>(base+tonumber_rva);
    atomic_api.pushinteger=reinterpret_cast<decltype(atomic_api.pushinteger)>(base+pushinteger_rva);
    atomic_api.pushvalue=reinterpret_cast<decltype(atomic_api.pushvalue)>(base+pushvalue_rva);
    atomic_api.pushcclosure=reinterpret_cast<decltype(atomic_api.pushcclosure)>(base+pushcclosure_rva);
    atomic_api.checkstack=reinterpret_cast<decltype(atomic_api.checkstack)>(base+checkstack_rva);
    atomic_api.createtable=reinterpret_cast<decltype(atomic_api.createtable)>(base+createtable_rva);
    atomic_api.setfield=reinterpret_cast<decltype(atomic_api.setfield)>(base+setfield_rva);
    atomic_api.pushboolean=reinterpret_cast<decltype(atomic_api.pushboolean)>(base+pushboolean_rva);
#endif
#ifdef PINJ_ATOMIC_MAP_API
    if(!verify_bytes(base,pushlstring_rva,pushlstring_bytes,sizeof(pushlstring_bytes)))throw std::runtime_error("Atomic string API mismatch");
    if(!verify_bytes(base,rawget_rva,rawget_bytes,sizeof(rawget_bytes)))throw std::runtime_error("Atomic raw lookup API mismatch");
    atomic_api.rawget=reinterpret_cast<decltype(atomic_api.rawget)>(base+rawget_rva);
    atomic_api.pushlstring=reinterpret_cast<decltype(atomic_api.pushlstring)>(base+pushlstring_rva);
#endif
    if(MH_Initialize()!=MH_OK)throw std::runtime_error("MinHook initialization failed");
    auto target=reinterpret_cast<void*>(base+pcall_rva);
    if(MH_CreateHook(target,reinterpret_cast<void*>(&hook),reinterpret_cast<void**>(&original))!=MH_OK)throw std::runtime_error("Cannot create Lua hook");
    atomic_api.pcall=original;
    // Pin the module: this MVP intentionally does not support unloading active hooks.
    HMODULE pinned=nullptr;
    if(!GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS|GET_MODULE_HANDLE_EX_FLAG_PIN,reinterpret_cast<LPCWSTR>(&hook),&pinned))throw std::runtime_error("Cannot pin bridge");
    if(MH_EnableHook(target)!=MH_OK)throw std::runtime_error("Cannot enable Lua hook");
    auto sd=pipe_security();if(!sd)throw std::runtime_error("Cannot create user-only pipe ACL");
    SECURITY_ATTRIBUTES sa{sizeof(sa),sd,FALSE};
    HANDLE pipe=CreateNamedPipeW(pipe_name(GetCurrentProcessId()).c_str(),pinj::pipe_open_mode|FILE_FLAG_FIRST_PIPE_INSTANCE,PIPE_TYPE_BYTE|PIPE_READMODE_BYTE|PIPE_WAIT|PIPE_REJECT_REMOTE_CLIENTS,1,65536,65536,0,&sa);
    LocalFree(sd);
    if(pipe==INVALID_HANDLE_VALUE)throw std::runtime_error("Cannot create command pipe");
    atomic_pipe=pipe;
    for(;;) {
      if(!pinj::pipe_connect(pipe)){DisconnectNamedPipe(pipe);continue;}
      DWORD client=0;
      if(!GetNamedPipeClientProcessId(pipe,&client)){DisconnectNamedPipe(pipe);continue;}
      std::string connection_owner;
      for(;;) {
        DWORD length=0;if(!pinj::pipe_transfer(pipe,&length,4,false)||!length||length>65541)break;
        std::string command(length,'\0');if(!pinj::pipe_transfer(pipe,command.data(),length,false))break;
        std::string response;
        if(command=="status")response="PInj bridge v0.8; profile="+std::string(fingerprint)+"; lua_pcall observations="+std::to_string(calls.load());
        else if(command=="atomic-capabilities")response=pinj::atomic_capabilities(atomic_api.getfield!=nullptr,atomic_api.pushlstring!=nullptr&&atomic_api.rawget!=nullptr);
        else if(command.rfind("atomic-bind ",0)==0) {
            const auto token=command.substr(12);
            if(!atomic_api.getfield)response=pinj::atomic_unavailable();
            else if(!pinj::SessionGate::valid_token(token))response=pinj::atomic_error("malformed");
            else {response=execute("",token,true);if(response.find("\"status\":\"bound\"")!=std::string::npos)connection_owner=token;}
        }
        else if(command.rfind("atomic ",0)==0) {
            if(!atomic_api.getfield)response=pinj::atomic_unavailable();
            else try {
                auto request=std::make_shared<pinj::AtomicRequest>(pinj::atomic_request(command.substr(7)));
                response=execute("",request->session,false,request);
            }catch(const std::exception& error){response=pinj::atomic_error(error.what());}
        }
        else if(command.rfind("bind ",0)==0||command.rfind("managed ",0)==0)response="ERROR\nLegacy in-client runtime removed; use the external typed bridge";
        else if(command.rfind("release ",0)==0) {
            std::lock_guard<std::mutex> lock(pending_mutex);
            if(gate.release(command.substr(8))){pinj::position_observer.deactivate();response="OK\nreleased";}
            else response="ERROR\nExternal session is not bound";
        }
        else if(command.rfind("exec ",0)==0)response=execute(command.substr(5));
        else response="ERROR\nUnknown command";
        length=static_cast<DWORD>(response.size());
        if(!pinj::pipe_transfer(pipe,&length,4,true)||!pinj::pipe_transfer(pipe,response.data(),length,true))break;
      }
      {std::lock_guard<std::mutex> lock(pending_mutex);if(!connection_owner.empty()){if(gate.release(connection_owner))pinj::position_observer.deactivate();}}
      DisconnectNamedPipe(pipe);
    }
 }catch(const std::exception& e){OutputDebugStringA((std::string("PInj: ")+e.what()+"\n").c_str());}
 return 0;
}
BOOL WINAPI DllMain(HINSTANCE instance,DWORD reason,LPVOID) {
    if(reason==DLL_PROCESS_ATTACH){DisableThreadLibraryCalls(instance);HANDLE t=CreateThread(nullptr,0,worker,nullptr,0,nullptr);if(!t)return FALSE;CloseHandle(t);}return TRUE;
}

