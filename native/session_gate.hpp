#pragma once
#include <cstdint>
#include <string>

namespace pinj {
// Caller serializes access. State addresses are opaque identities, never dereferenced.
class SessionGate {
    std::uintptr_t state_=0;
    unsigned long thread_=0;
    std::string token_;
    std::uint64_t renewed_=0;
    static constexpr std::uint64_t lease_ms=10000;
    void expire(std::uint64_t now) {
        if(!token_.empty() && now>=renewed_ && now-renewed_>=lease_ms) {token_.clear();state_=0;thread_=0;}
    }
public:
    static bool valid_token(const std::string& token) {
        if(token.size()!=32) return false;
        for(char c:token) if(!((c>='0'&&c<='9')||(c>='a'&&c<='f'))) return false;
        return true;
    }
    bool bind(const std::string& token,std::uintptr_t state,unsigned long thread,std::uint64_t now=0) {
        expire(now);
        if(!valid_token(token)||!state||!thread) return false;
        if(!token_.empty()) return matches(token,state,thread,now);
        token_=token;state_=state;thread_=thread;renewed_=now;return true;
    }
    bool matches(const std::string& token,std::uintptr_t state,unsigned long thread,std::uint64_t now=0) {
        expire(now);
        return !token_.empty()&&token==token_&&state==state_&&thread==thread_;
    }
    bool owns(const std::string& token,std::uint64_t now=0) {expire(now);return !token_.empty()&&token==token_;}
    bool renew(const std::string& token,std::uint64_t now) {if(!owns(token,now))return false;renewed_=now;return true;}
    bool release(const std::string& token) {
        if(!owns(token)) return false;
        token_.clear();state_=0;thread_=0;return true;
    }
    bool bound() const {return !token_.empty();}
};
}
