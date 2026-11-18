#pragma once
#include <string>
namespace pinj {
inline std::string atomic_capabilities(bool enabled=false,bool map=false) {
    const std::string prefix=R"({"version":1,"available":)";
    const std::string limits=R"(,"limits":{"request_bytes":4096,"response_bytes":65536,"events":128,"pending":64,"request_ids":65536,"ttl_ms":1000})";
    if(enabled){std::string result=prefix+R"(true,"operations":["snapshot","events","walk","cancel"],"experimental":true,"event_mode":"snapshot_samples")"+limits+"}";
        if(map){const auto at=result.find("\"cancel\"");result.insert(at+8,",\"items.read\",\"items.command\",\"map.capabilities\",\"map.export\",\"observer.start\",\"observer.stop\",\"observer.events\",\"observer.mark\"");}
        if(map)result.insert(result.size()-1,",\"item_mechanisms\":[0,1,2,3,4]");
        const auto at=result.find("\"cancel\"");result.insert(at+8,",\"walk_transition\"");
        return result;
    }
    return prefix+R"(false,"operations":[])"+limits+R"(,"reason":"External runtime unavailable: this profile has no complete source-free Lua API bindings."})";
}
inline std::string atomic_unavailable() {
    return R"({"version":1,"error":"unsupported","reason":"Native primitive dispatch is unavailable for this profile; no Lua source was executed."})";
}
}
