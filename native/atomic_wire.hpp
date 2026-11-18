#pragma once
#include "atomic_lua.hpp"
#include "atomic_map.hpp"
#include "atomic_observer.hpp"
#include "atomic_items.hpp"
#include "session_gate.hpp"
#include <map>
#include <deque>
#include <stdexcept>

namespace pinj {
// Closed-schema JSON reader: only strings, safe nonnegative integers and objects
// are valid request values. Arrays, booleans, floats and null are never needed.
struct AtomicValue {
    enum Kind { object, string, integer } kind=object;
    std::map<std::string,AtomicValue> fields;
    std::string text;
    std::uint64_t number=0;
};
class AtomicJson {
    const std::string& input;std::size_t at=0;
    void ws(){while(at<input.size()&&(input[at]==' '||input[at]=='\n'||input[at]=='\r'||input[at]=='\t'))++at;}
    char take(){if(at==input.size())throw std::runtime_error("malformed");return input[at++];}
    std::string str(){
        if(take()!='"')throw std::runtime_error("malformed");
        std::string result;
        for(;;){char ch=take();if(ch=='"')return result;
            if(static_cast<unsigned char>(ch)<32||static_cast<unsigned char>(ch)>126)throw std::runtime_error("malformed");
            if(ch=='\\'){
                ch=take();
                if(ch=='u'){
                    unsigned value=0;for(int i=0;i<4;++i){char hex=take();value*=16;
                        if(hex>='0'&&hex<='9')value+=hex-'0';else if(hex>='a'&&hex<='f')value+=hex-'a'+10;else if(hex>='A'&&hex<='F')value+=hex-'A'+10;else throw std::runtime_error("malformed");}
                    if(value<32||value>126)throw std::runtime_error("malformed");
                    ch=static_cast<char>(value);
                }else if(ch!='"'&&ch!='\\'&&ch!='/')throw std::runtime_error("malformed");
            }
            result+=ch;if(result.size()>128)throw std::runtime_error("malformed");
        }
    }
    AtomicValue value(int depth){
        if(depth>8)throw std::runtime_error("malformed");
        ws();AtomicValue result;
        if(at==input.size())throw std::runtime_error("malformed");
        if(input[at]=='{'){
            ++at;ws();if(at<input.size()&&input[at]=='}'){++at;return result;}
            for(;;){ws();const auto key=str();ws();if(take()!=':')throw std::runtime_error("malformed");
                if(!result.fields.emplace(key,value(depth+1)).second)throw std::runtime_error("malformed");
                ws();char end=take();if(end=='}')return result;if(end!=',')throw std::runtime_error("malformed");}
        }
        if(input[at]=='"'){result.kind=AtomicValue::string;result.text=str();return result;}
        result.kind=AtomicValue::integer;
        if(input[at]<'0'||input[at]>'9')throw std::runtime_error("malformed");
        if(input[at]=='0'&&at+1<input.size()&&input[at+1]>='0'&&input[at+1]<='9')throw std::runtime_error("malformed");
        while(at<input.size()&&input[at]>='0'&&input[at]<='9'){
            result.number=result.number*10+input[at++]-'0';
            if(result.number>9007199254740991ULL)throw std::runtime_error("malformed");
        }
        return result;
    }
public:
    explicit AtomicJson(const std::string& source):input(source){}
    AtomicValue parse(){if(input.empty()||input.size()+7>4096)throw std::runtime_error("malformed");auto result=value(0);ws();if(at!=input.size()||result.kind!=AtomicValue::object)throw std::runtime_error("malformed");return result;}
};
inline void atomic_fields(const AtomicValue& v,std::initializer_list<const char*> names){
    if(v.kind!=AtomicValue::object||v.fields.size()!=names.size())throw std::runtime_error("malformed");
    for(const char* name:names)if(!v.fields.count(name))throw std::runtime_error("malformed");
}
inline std::uint64_t atomic_int(const AtomicValue& v,std::uint64_t low,std::uint64_t high){
    if(v.kind!=AtomicValue::integer||v.number<low||v.number>high)throw std::runtime_error("malformed");
    return v.number;
}
inline std::string atomic_string(const AtomicValue& v){if(v.kind!=AtomicValue::string)throw std::runtime_error("malformed");return v.text;}
struct AtomicRequest {
    std::string id,session,operation,target,basename,view_ref;
    std::uint64_t generation=0,deadline=0,seq=0,after=0,limit=0;
    int direction=0,mechanism=0,item_slot=0,tool_index=0;AtomicPosition origin,position;
};
inline AtomicRequest atomic_request(const std::string& input){
    const auto v=AtomicJson(input).parse();atomic_fields(v,{"version","request_id","session","generation","deadline_ms","operation","payload"});
    if(atomic_int(v.fields.at("version"),0,9007199254740991ULL)!=1)throw std::runtime_error("unsupported_version");
    AtomicRequest r;r.id=atomic_string(v.fields.at("request_id"));r.session=atomic_string(v.fields.at("session"));
    if(!SessionGate::valid_token(r.id)||!SessionGate::valid_token(r.session))throw std::runtime_error("malformed");
    r.generation=atomic_int(v.fields.at("generation"),1,2147483647);r.deadline=atomic_int(v.fields.at("deadline_ms"),0,9007199254740991ULL);
    r.operation=atomic_string(v.fields.at("operation"));const auto& p=v.fields.at("payload");
    if(r.operation=="snapshot"||r.operation=="map.capabilities"||r.operation=="observer.start"||r.operation=="observer.stop"||r.operation=="observer.mark")atomic_fields(p,{});
    else if(r.operation=="map.export"){
        atomic_fields(p,{"basename"});r.basename=atomic_string(p.fields.at("basename"));
        const std::string prefix="otclientbot-map-";
        if(r.basename.size()!=prefix.size()+32+1+32+5||r.basename.compare(0,prefix.size(),prefix)!=0||r.basename.substr(r.basename.size()-5)!=".otmm"||r.basename[prefix.size()+32]!='-'||!SessionGate::valid_token(r.basename.substr(prefix.size(),32))||!SessionGate::valid_token(r.basename.substr(prefix.size()+33,32)))throw std::runtime_error("malformed");
    }
    else if(r.operation=="items.read"||r.operation=="items.command"){
        const bool read=r.operation=="items.read";
        if(read)atomic_fields(p,{"position","expected_origin","snapshot_seq"});
        else atomic_fields(p,{"view_ref","mechanism","target_slot","tool_index","expected_origin","snapshot_seq"});
        r.seq=atomic_int(p.fields.at("snapshot_seq"),1,9007199254740991ULL);
        const auto& pos=p.fields.at("expected_origin");atomic_fields(pos,{"x","y","z"});
        r.origin={static_cast<int>(atomic_int(pos.fields.at("x"),0,65535)),static_cast<int>(atomic_int(pos.fields.at("y"),0,65535)),static_cast<int>(atomic_int(pos.fields.at("z"),0,15))};
        if(read){const auto& tile=p.fields.at("position");atomic_fields(tile,{"x","y","z"});r.position={static_cast<int>(atomic_int(tile.fields.at("x"),0,65535)),static_cast<int>(atomic_int(tile.fields.at("y"),0,65535)),static_cast<int>(atomic_int(tile.fields.at("z"),0,15))};}
        else{r.view_ref=atomic_string(p.fields.at("view_ref"));if(!SessionGate::valid_token(r.view_ref))throw std::runtime_error("malformed");r.mechanism=static_cast<int>(atomic_int(p.fields.at("mechanism"),0,4));r.item_slot=static_cast<int>(atomic_int(p.fields.at("target_slot"),0,32));r.tool_index=static_cast<int>(atomic_int(p.fields.at("tool_index"),0,128));}
    }
    else if(r.operation=="walk"||r.operation=="walk_transition"){
        atomic_fields(p,{"direction","expected_origin","snapshot_seq"});r.direction=static_cast<int>(atomic_int(p.fields.at("direction"),0,7));
        r.seq=atomic_int(p.fields.at("snapshot_seq"),1,9007199254740991ULL);const auto& pos=p.fields.at("expected_origin");atomic_fields(pos,{"x","y","z"});
        r.origin={static_cast<int>(atomic_int(pos.fields.at("x"),0,65535)),static_cast<int>(atomic_int(pos.fields.at("y"),0,65535)),static_cast<int>(atomic_int(pos.fields.at("z"),0,15))};
    }else if(r.operation=="events"||r.operation=="observer.events"){
        atomic_fields(p,{"after","limit"});r.after=atomic_int(p.fields.at("after"),0,9007199254740991ULL);r.limit=atomic_int(p.fields.at("limit"),1,128);
    }else if(r.operation=="cancel"){
        atomic_fields(p,{"request_id"});r.target=atomic_string(p.fields.at("request_id"));if(!SessionGate::valid_token(r.target))throw std::runtime_error("malformed");
    }else throw std::runtime_error("unsupported");
    return r;
}
inline std::string atomic_quote(const std::string& value){
    const char* hex="0123456789abcdef";std::string result="\"";
    for(unsigned char ch:value){if(ch=='"'||ch=='\\'){result+='\\';result+=static_cast<char>(ch);}else if(ch<32){result+="\\u00";result+=hex[ch>>4];result+=hex[ch&15];}else result+=static_cast<char>(ch);}
    return result+'"';
}
inline std::string atomic_error(const char* code){return "{\"version\":1,\"error\":"+atomic_quote(code)+"}";}
inline std::string atomic_position(const AtomicPosition& p){return "{\"x\":"+std::to_string(p.x)+",\"y\":"+std::to_string(p.y)+",\"z\":"+std::to_string(p.z)+"}";}
inline std::string atomic_observation(const AtomicObservation& o){
    std::string result=std::string("{\"online\":")+(o.online?"true":"false")+",\"name\":"+atomic_quote(o.name)+",\"position\":"+(o.has_player?atomic_position(o.position):"null")+",\"step_ms\":"+(o.step_ms?std::to_string(o.step_ms):"null");
    const char* names[]={"health","max_health","mana","max_mana"};const int values[]={o.health,o.max_health,o.mana,o.max_mana};
    for(int i=0;i<4;++i)result+=std::string(",\"")+names[i]+"\":"+(values[i]<0?"null":std::to_string(values[i]));
    return result+"}";
}
inline std::string item_json(const AtomicItem& item,int index){return "{\"index\":"+std::to_string(index)+",\"id\":"+std::to_string(item.id)+",\"count\":"+std::to_string(item.count)+",\"slot\":"+std::to_string(item.slot)+",\"container\":"+std::to_string(item.container)+",\"movable\":"+(item.movable?"true":"false")+"}";}
inline std::string items_json(const AtomicItemView& view){
 std::string result="{\"position\":"+atomic_position(view.tile.position)+",\"walkable\":"+(view.tile.walkable?"true":"false")+",\"pathable\":"+(view.tile.pathable?"true":"false")+",\"items\":[";
 for(int i=0;i<view.tile.count;++i){if(i)result+=',';result+=item_json(view.tile.items[i],i);}result+="],\"inventory\":[";
 for(int i=0;i<view.count;++i){if(i)result+=',';result+=item_json(view.inventory[i],i);}return result+"],\"truncated\":"+(view.truncated?"true":"false")+"}";
}
// Caller serializes this state with the attachment gate. One in-flight operation
// is sufficient; sample events are not a substitute for position callbacks.
class AtomicSession {
    struct Sample{std::uint64_t seq,ms;AtomicObservation observation;};
    std::string session;std::map<std::string,int> ids;std::deque<Sample> samples;
    std::uint64_t sequence=0;std::string character;bool movement_pending=false,mutation_uncertain=false;AtomicPosition movement_origin;
    AtomicTile pending_item_tile;bool item_pending=false;AtomicItemView item_view;std::string item_view_ref;std::uint64_t item_view_at=0;
public:
    void reset(const std::string& token){session=token;ids.clear();samples.clear();sequence=0;character.clear();movement_pending=false;mutation_uncertain=false;item_pending=false;item_view_ref.clear();}
    std::string reserve(const AtomicRequest& r,std::uint64_t now){
        if(r.session!=session)return "stale_session";
        if(r.generation!=1)return "stale_generation";
        if(r.deadline<=now||r.deadline-now>1000)return "expired";
        if(ids.count(r.id))return "duplicate";
        if(ids.size()>=65536)return "replay_capacity";
        ids.emplace(r.id,0);if(position_observer.active)position_observer.lease=now;return "";
    }
    std::string dispatch(const AtomicRequest& r,const AtomicLuaApi& api,lua_State* L,std::uint64_t now,std::uint64_t (*clock)()=nullptr,bool (*authorized)()=nullptr){
        if(r.session!=session)return atomic_error("stale_session");
        if(r.generation!=1)return atomic_error("stale_generation");
        if(r.deadline<=now)return atomic_error("expired");
        if(ids.at(r.id)==2)return atomic_error("cancelled");
        const bool walking=r.operation=="walk"||r.operation=="walk_transition";
        if(!walking)ids.at(r.id)=3;
        const std::string prefix="{\"version\":1,\"request_id\":"+atomic_quote(r.id)+",\"session\":"+atomic_quote(r.session)+",\"generation\":1,\"monotonic_ms\":"+std::to_string(now);
        if(r.operation=="items.read"||r.operation=="items.command"){
            const bool read=r.operation=="items.read";
            if(samples.empty()||r.seq!=sequence||now<samples.back().ms||now-samples.back().ms>1000||!samples.back().observation.has_player)return atomic_error("stale_snapshot");
            if(!same_position(r.origin,samples.back().observation.position))return atomic_error("precondition");
            if(!read&&(movement_pending||mutation_uncertain))return atomic_error("unknown");
            if(!read&&(r.view_ref!=item_view_ref||now<item_view_at||now-item_view_at>1000))return atomic_error("stale_view");
            AtomicItemsCall call;call.api=&api;call.name=samples.back().observation.name;call.origin=r.origin;call.position=read?r.position:item_view.tile.position;call.deadline=r.deadline;call.clock=clock;call.authorized=authorized;
            if(!read){call.expected=&item_view;call.mechanism=r.mechanism;call.target=r.item_slot;call.tool=r.tool_index;}
            const auto result=invoke_items(call,L);
            if(read&&std::strcmp(result.result.code,"ok")==0){
                if(item_pending&&!mutation_uncertain&&same_position(pending_item_tile.position,result.view.tile.position)&&!same_tile(pending_item_tile,result.view.tile)){movement_pending=false;item_pending=false;}
                item_view=result.view;item_view_ref=r.id;item_view_at=now;
            }
            if(result.result.dispatched){ids.at(r.id)=1;movement_pending=true;movement_origin=r.origin;item_pending=true;pending_item_tile=item_view.tile;if(std::strcmp(result.result.code,"unknown")==0)mutation_uncertain=true;}
            return prefix+",\"status\":"+atomic_quote(result.result.code)+",\"view_ref\":"+atomic_quote(read?r.id:r.view_ref)+",\"view\":"+(read?items_json(result.view):"null")+",\"error_message\":"+atomic_quote(result.result.error)+"}";
        }
        if(r.operation=="observer.start"||r.operation=="observer.stop"){
            const auto result=invoke_observer(api,L,r.operation=="observer.start",clock,authorized);
            return prefix+",\"status\":"+atomic_quote(result.code)+",\"error_message\":"+atomic_quote(result.error)+",\"latest_seq\":"+std::to_string(position_observer.sequence)+"}";
        }
        if(r.operation=="observer.mark"){
            auto& o=position_observer;if(o.owner!=L||!o.active)return atomic_error("unsupported");
            const auto before=o.sequence;const auto current=invoke_atomic(api,L);
            if(!character.empty()&&((current.game_context&&!current.online)||(current.has_player&&character!=current.name))){session.clear();o.deactivate();return atomic_error("stale_session");}
            if(std::strcmp(current.code,"ok")!=0||!current.has_player)return atomic_error("precondition");
            AtomicPosition position=current.position;
            if(o.sequence>before){if(!o.count||o.events[(o.head+o.count-1)%o.events.size()].seq!=o.sequence)return atomic_error("event_gap");position=o.events[(o.head+o.count-1)%o.events.size()].position;}
            return prefix+",\"status\":\"ok\",\"position\":"+atomic_position(position)+",\"latest_seq\":"+std::to_string(o.sequence)+"}";
        }
        if(r.operation=="observer.events"){
            const auto& o=position_observer;
            if(o.owner!=L||!o.active)return atomic_error("unsupported");
            std::string events="[";std::uint64_t count=0;
            for(std::size_t i=0;i<o.count&&count<r.limit;++i){const auto& event=o.events[(o.head+i)%o.events.size()];if(event.seq<=r.after)continue;
                if(count++)events+=',';
                events+="{\"seq\":"+std::to_string(event.seq)+",\"monotonic_ms\":"+std::to_string(event.at)+",\"position\":"+atomic_position(event.position)+",\"previous\":"+atomic_position(event.previous)+"}";
            }
            return prefix+",\"status\":\"ok\",\"latest_seq\":"+std::to_string(o.sequence)+",\"dropped_total\":"+std::to_string(o.dropped)+",\"oldest_seq\":"+std::to_string(o.count?o.events[o.head].seq:o.sequence+1)+",\"events\":"+events+"]}";
        }
        if(r.operation=="map.capabilities"||r.operation=="map.export"){
            const bool exporting=r.operation=="map.export";
            if(exporting&&(samples.empty()||!samples.back().observation.has_player))return atomic_error("precondition");
            const auto map=invoke_atomic_map(api,L,exporting?r.basename.c_str():nullptr,samples.empty()?nullptr:samples.back().observation.name,r.deadline,clock,authorized);
            const std::string value=exporting?("{\"schema_version\":1,\"write_dir\":"+atomic_quote(map.directory)+",\"basename\":"+atomic_quote(r.basename)+",\"exporter\":"+atomic_quote(std::string(map.name)+".saveOtmm")+"}"):
                (std::string("{\"schema_version\":1,\"otmm_exporter\":")+(map.exporter?atomic_quote(std::string(map.name)+".saveOtmm"):"false")+",\"write_dir_available\":"+(map.write_dir?"true":"false")+",\"spawn_adapter\":false}");
            return prefix+",\"status\":"+atomic_quote(map.code)+",\"value\":"+value+",\"error_message\":"+atomic_quote(map.error)+"}";
        }
        if(r.operation=="cancel"){
            const auto target=ids.find(r.target);
            const char* status="not_found";
            if(target!=ids.end()&&target->second==0){target->second=2;status="cancelled";}
            else if(target!=ids.end()&&target->second==1)status="too_late";
            return prefix+",\"status\":"+atomic_quote(status)+"}";
        }
        if(r.operation=="events"){
            if(r.after>sequence)return atomic_error("event_gap");
            const auto oldest=samples.empty()?sequence+1:samples.front().seq;
            std::string events="[";bool first=true;std::uint64_t count=0;
            for(const auto& sample:samples)if(sample.seq>r.after&&count++<r.limit){
                if(events.size()>58000)break;
                if(!first)events+=',';
                first=false;
                events+="{\"kind\":\"snapshot_sample\",\"seq\":"+std::to_string(sample.seq)+",\"monotonic_ms\":"+std::to_string(sample.ms)+",\"payload\":"+atomic_observation(sample.observation)+"}";
            }
            return prefix+",\"status\":\"ok\",\"oldest_seq\":"+std::to_string(oldest)+",\"latest_seq\":"+std::to_string(sequence)+",\"dropped\":"+std::to_string(oldest>r.after+1?oldest-r.after-1:0)+",\"events\":"+events+"]}";
        }
        if(walking){
            if(movement_pending||mutation_uncertain)return atomic_error("unknown");
            if(samples.empty()||r.seq!=sequence||now<samples.back().ms||now-samples.back().ms>1000)return atomic_error("stale_snapshot");
            if(!samples.back().observation.has_player||!same_position(r.origin,samples.back().observation.position))return atomic_error("precondition");
        }
        const auto observed=invoke_atomic(api,L,walking,r.direction,r.origin,samples.empty()?nullptr:samples.back().observation.name,r.deadline,clock,authorized,r.operation=="walk_transition");
        if(!character.empty()&&((observed.game_context&&!observed.online)||(observed.has_player&&character!=observed.name))){session.clear();position_observer.deactivate();return atomic_error("stale_session");}
        if(observed.online&&observed.has_player&&character.empty())character=observed.name;
        ids.at(r.id)=observed.dispatched?1:3;
        if(observed.dispatched){movement_pending=true;movement_origin=r.origin;item_pending=false;}
        if(r.operation=="snapshot"&&std::strcmp(observed.code,"ok")==0){if(movement_pending&&observed.has_player&&!same_position(movement_origin,observed.position)){movement_pending=false;item_pending=false;}
            samples.push_back({++sequence,now,observed});if(samples.size()>128)samples.pop_front();}
        return prefix+",\"status\":"+atomic_quote(observed.code)+",\"snapshot_seq\":"+std::to_string(sequence)+",\"observation\":"+atomic_observation(observed)+",\"error_message\":"+atomic_quote(observed.error)+"}";
    }
};
}
