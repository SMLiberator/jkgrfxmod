#pragma once
// JkGRfxMod's opt-in colored *existing* Sith Thing lights.
// Sidecar state; do not write into overlapping SithThing type unions,
// overload explosion flashrgb, or allocate an extra rdCamera light.
// RGB input 0..1 is hue-only (max channel 1; zero maps to white).
#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <mutex>
#include <unordered_map>
#include <vector>

namespace jkgm { namespace thing_light_rgb {
using rgb=std::array<float,3>;
constexpr rgb white{{1.0f,1.0f,1.0f}};
inline bool normalize(rgb const &in,rgb &out)
{
    for(float v:in)if(!std::isfinite(v) || v<0.0f || v>1.0f)
        return false;
    float const max_channel=std::max({in[0],in[1],in[2]});
    if(max_channel<=0.0f){out=white;return true;}
    for(int n=0;n<3;++n)out[n]=in[n]/max_channel;
    return true;
}
inline rgb lerp(rgb const &a,rgb const &b,float t)
{
    float const x=std::max(0.0f,std::min(1.0f,t));
    return {{a[0]+(b[0]-a[0])*x,
             a[1]+(b[1]-a[1])*x,
             a[2]+(b[2]-a[2])*x}};
}
struct record {
    uintptr_t address=0;
    uint32_t signature=0;
    bool is_template=false;
    bool static_template=false; // Static.jkl persists across level loads
    rgb from=white;
    rgb to=white;
    double started=0;
    double duration=0;
    rgb at(double now) const {
        if(duration<=0.0 || now>=started+duration)return to;
        if(now<=started)return from;
        return lerp(from,to,float((now-started)/duration));
    }
};
class registry {
    mutable std::mutex mutex_;
    uintptr_t world_=0;
    uintptr_t static_world_=0;
    std::unordered_map<uintptr_t,record> records_;
public:
    // Static.jkl and a level JKL are parsed into two distinct SithWorlds.
    // Switching the active level must not discard static template colors.
    void prepare_static_world(uintptr_t world) {
        if(!world)return;
        std::lock_guard<std::mutex> lock(mutex_);
        if(static_world_!=world) {
            records_.clear();
            static_world_=world;
            world_=0;
        }
    }
    void prepare_world(uintptr_t world) {
        if(!world)return;
        std::lock_guard<std::mutex> lock(mutex_);
        if(world_!=world) {
            for(auto it=records_.begin();it!=records_.end();) {
                if(it->second.static_template)++it;
                else it=records_.erase(it);
            }
            world_=world;
        }
    }
    void clear() {
        std::lock_guard<std::mutex> lock(mutex_);
        records_.clear();world_=static_world_=0;
    }
    bool set(uintptr_t address,uint32_t signature,bool is_template,
             rgb const &input,double fade,double now,
             bool static_template=false) {
        rgb target{};
        if(address<0x10000U || !normalize(input,target) ||
           !std::isfinite(fade) || fade<0.0 || fade>3600.0 ||
           !std::isfinite(now))return false;
        std::lock_guard<std::mutex> lock(mutex_);
        auto &v=records_[address];
        rgb const before=v.address==address &&
            v.signature==signature && v.is_template==is_template
            ?v.at(now):white;
        v.address=address;v.signature=signature;v.is_template=is_template;
        v.static_template=is_template && static_template;
        v.from=before;v.to=target;v.started=now;v.duration=fade;
        if(fade<=0.0)v.from=target;
        return true;
    }
    bool get(uintptr_t address,uint32_t signature,
             rgb &out,double now) const {
        out=white;
        if(!std::isfinite(now))return false;
        std::lock_guard<std::mutex> lock(mutex_);
        auto it=records_.find(address);
        if(it==records_.end() ||
           (!it->second.is_template && it->second.signature!=signature))
            return false;
        out=it->second.at(now);
        return true;
    }
    // Native SetThingBasedOn has already copied the source, including the
    // new signature of the destination. Make its light hue follow the source
    // template, but never let a recycled destination inherit its old value.
    void inherit(uintptr_t destination,uint32_t signature,
                 uintptr_t source,double now) {
        if(destination<0x10000U || !std::isfinite(now))return;
        std::lock_guard<std::mutex> lock(mutex_);
        rgb initial=white;
        bool have=false;
        auto it=records_.find(source);
        if(it!=records_.end()) {
            initial=it->second.at(now);
            have=true;
        }
        records_.erase(destination);
        if(have) {
            record r{};
            r.address=destination;r.signature=signature;r.is_template=false;
            r.from=r.to=initial;r.started=now;
            records_.emplace(destination,r);
        }
    }
    void erase(uintptr_t address) {
        std::lock_guard<std::mutex> lock(mutex_);
        records_.erase(address);
    }
    std::vector<record> things(double now) const {
        std::lock_guard<std::mutex> lock(mutex_);
        std::vector<record> result;
        result.reserve(records_.size());
        for(auto const &p:records_) {
            auto r=p.second;
            if(r.is_template)continue;
            rgb const current=r.at(now);
            // Default and achromatic lights need no GPU sidecar work.
            if(std::max({current[0],current[1],current[2]})-
               std::min({current[0],current[1],current[2]})<=0.0001f)continue;
            r.from=r.to=current;r.duration=0.0;
            result.emplace_back(r);
        }
        return result;
    }
};
inline registry &get_registry() {
    static registry instance;
    return instance;
}
}} // namespaces
