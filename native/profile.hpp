#pragma once
#include <windows.h>
#include <bcrypt.h>
#include <fstream>
#include <vector>
#include <string>
#include <stdexcept>
#pragma comment(lib,"bcrypt.lib")
#ifdef PINJ_PROFILE_HEADER
#include PINJ_PROFILE_HEADER
#else
#include "profile_config.hpp"
#endif
inline std::string hash_file(const std::wstring& path) {
    std::ifstream f(path, std::ios::binary);
    if(!f) throw std::runtime_error("Cannot open target executable");
    BCRYPT_ALG_HANDLE alg=nullptr; BCRYPT_HASH_HANDLE hash=nullptr;
    if(BCryptOpenAlgorithmProvider(&alg,BCRYPT_SHA256_ALGORITHM,nullptr,0)<0) throw std::runtime_error("SHA256 provider failed");
    if(BCryptCreateHash(alg,&hash,nullptr,0,nullptr,0,0)<0){BCryptCloseAlgorithmProvider(alg,0);throw std::runtime_error("SHA256 creation failed");}
    char buf[65536]; bool ok=true;
    while(f.read(buf,sizeof(buf)) || f.gcount()) if(BCryptHashData(hash,reinterpret_cast<PUCHAR>(buf),static_cast<ULONG>(f.gcount()),0)<0){ok=false;break;}
    unsigned char out[32];
    ok=ok && f.eof() && BCryptFinishHash(hash,out,sizeof(out),0)>=0;
    BCryptDestroyHash(hash);BCryptCloseAlgorithmProvider(alg,0);
    if(!ok) throw std::runtime_error("SHA256 read failed");
    std::string result; const char* hex="0123456789abcdef";
    for(auto v:out){result+=hex[v>>4];result+=hex[v&15];} return result;
}
inline std::wstring pipe_name(DWORD pid){return L"\\\\.\\pipe\\PInj-"+std::to_wstring(pid);}

