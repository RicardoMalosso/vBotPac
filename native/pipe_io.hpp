#pragma once
#include <windows.h>

namespace pinj {
// PeekNamedPipe on this handle must never serialize behind the worker's read.
// PIPE_NOWAIT does not replace overlapped I/O.
inline constexpr DWORD pipe_open_mode = PIPE_ACCESS_DUPLEX | FILE_FLAG_OVERLAPPED;

struct PipeOperation {
    OVERLAPPED io{};
    PipeOperation() { io.hEvent=CreateEventW(nullptr,TRUE,FALSE,nullptr); }
    ~PipeOperation() { if(io.hEvent)CloseHandle(io.hEvent); }
    PipeOperation(const PipeOperation&)=delete;
    PipeOperation& operator=(const PipeOperation&)=delete;
    bool finish(HANDLE pipe,BOOL submitted,DWORD error,DWORD& size,DWORD timeout) {
        if(!submitted&&error!=ERROR_IO_PENDING)return false;
        if(!submitted&&WaitForSingleObject(io.hEvent,timeout)!=WAIT_OBJECT_0){
            // The OVERLAPPED and buffer must outlive cancelled I/O as well.
            CancelIoEx(pipe,&io);
            GetOverlappedResult(pipe,&io,&size,TRUE);
            return false;
        }
        return GetOverlappedResult(pipe,&io,&size,FALSE)!=FALSE;
    }
};

// Only the IPC worker waits here. Lua callbacks may PeekNamedPipe concurrently.
inline bool pipe_connect(HANDLE pipe,DWORD timeout=INFINITE) {
    PipeOperation operation;if(!operation.io.hEvent)return false;
    const BOOL submitted=ConnectNamedPipe(pipe,&operation.io);
    const DWORD error=submitted?ERROR_SUCCESS:GetLastError();
    if(!submitted&&error==ERROR_PIPE_CONNECTED)return true;
    DWORD size=0;
    return operation.finish(pipe,submitted,error,size,timeout);
}

inline bool pipe_transfer(HANDLE pipe,void* data,DWORD size,bool write,DWORD timeout=INFINITE) {
    auto* bytes=static_cast<unsigned char*>(data);
    while(size){
        PipeOperation operation;if(!operation.io.hEvent)return false;
        DWORD transferred=0;
        const BOOL submitted=write?WriteFile(pipe,bytes,size,&transferred,&operation.io)
                                  :ReadFile(pipe,bytes,size,&transferred,&operation.io);
        const DWORD error=submitted?ERROR_SUCCESS:GetLastError();
        if(!operation.finish(pipe,submitted,error,transferred,timeout)||!transferred)return false;
        bytes+=transferred;size-=transferred;
    }
    return true;
}
}
