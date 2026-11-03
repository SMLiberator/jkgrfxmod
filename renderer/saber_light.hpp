#pragma once
// Per-Thing saber-light overrides. JK's 0.6 maximum and length fade remain
// native; a reversible sidecar multiplier is applied after Thing updates.
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <unordered_map>
#include <vector>

namespace jkgm {
namespace saber_light {
constexpr float vanilla_max=0.6f;
constexpr float maximum_allowed=16.0f;
constexpr uintptr_t native_intensity_offset=0x1a4U;
constexpr uintptr_t native_blade_position_offset=0x198U;

struct record {
    uintptr_t thing=0;
    uint32_t identity=0;
    float maximum=vanilla_max;
    float unscaled_intensity=0.0f;
};

inline bool valid_maximum(float v) {
    return std::isfinite(v) && v>=0.0f && v<=maximum_allowed;
}
inline float scale_intensity(float vanilla,float maximum) {
    if(!std::isfinite(vanilla) || vanilla<=0.0f || maximum<=0.0f)
        return 0.0f;
    return std::min(maximum_allowed,vanilla*(maximum/vanilla_max));
}

class registry {
    uintptr_t world_=0;
    std::unordered_map<uintptr_t,record> records_;
public:
    bool empty() const {return records_.empty();}
    void clear() {records_.clear();world_=0;}
    void prepare_world(uintptr_t world) {
        if(world && world!=world_) {records_.clear();world_=world;}
    }
    void erase(uintptr_t thing) {records_.erase(thing);}
    std::vector<record> snapshot() const {
        std::vector<record> out;
        out.reserve(records_.size());
        for(auto const &entry:records_)out.push_back(entry.second);
        return out;
    }
    bool get(uintptr_t thing,uint32_t identity,record &out) const {
        auto const it=records_.find(thing);
        if(it==records_.end() || it->second.identity!=identity)return false;
        out=it->second;
        return true;
    }
    // 'current_native' is used only for a new owner. Subsequent calls
    // use the saved UNMODIFIED value, not our previously scaled output.
    bool set(uintptr_t thing,uint32_t identity,float maximum,
             float current_native,float &adjusted) {
        if(thing<0x10000U || !identity || !valid_maximum(maximum) ||
           !std::isfinite(current_native) || current_native<0.0f)
            return false;
        record old{};
        float const original=get(thing,identity,old)
            ?old.unscaled_intensity:current_native;
        if(maximum==vanilla_max) {
            records_.erase(thing); // Restore unmodified JK behavior.
            adjusted=original;
        } else {
            records_[thing]={thing,identity,maximum,original};
            adjusted=scale_intensity(original,maximum);
        }
        return true;
    }
    // Called after native JK advances/recomputes the blade length.
    bool capture_native(uintptr_t thing,uint32_t identity,float native,
                        float &adjusted) {
        if(!std::isfinite(native) || native<0.0f)return false;
        auto const it=records_.find(thing);
        if(it==records_.end() || it->second.identity!=identity)return false;
        it->second.unscaled_intensity=native;
        adjusted=scale_intensity(native,it->second.maximum);
        return true;
    }
};
} // namespace saber_light
} // namespace jkgm
