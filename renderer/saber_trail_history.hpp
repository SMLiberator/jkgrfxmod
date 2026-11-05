#pragma once
// Time-delayed *blade direction*, not a static world-space smear.
// The trailing saber rotates about the current hilt and catches up when the
// blade stops. These pure math/history helpers do not require OpenGL or JK.
#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <unordered_map>

namespace jkgm {
using saber_trail_vec3=std::array<float,3>;

inline bool saber_trail_direction(saber_trail_vec3 const &base,
                                 saber_trail_vec3 const &tip,
                                 saber_trail_vec3 &unit)
{
    double length2=0.0;
    for(int i=0;i<3;++i) {
        if(!std::isfinite(base[i]) || !std::isfinite(tip[i]))return false;
        unit[i]=tip[i]-base[i];
        length2+=double(unit[i])*unit[i];
    }
    if(!std::isfinite(length2) || length2<1.e-8 || length2>1.e10)return false;
    float inv=float(1.0/std::sqrt(length2));
    for(float &x:unit)x*=inv;
    return true;
}

struct saber_trail_key {
    uint64_t owner=0; // native SithThing address + its generation signature
    uint8_t blade=0;  // right and left saber remain independent
    bool operator==(saber_trail_key const &rhs) const {
        return owner==rhs.owner && blade==rhs.blade;
    }
};
struct saber_trail_key_hash {
    size_t operator()(saber_trail_key const &key) const {
        uint64_t n=key.owner^(uint64_t(key.blade+1U)*0x9e3779b97f4a7c15ULL);
        n^=n>>33; n*=0xff51afd7ed558ccdULL;
        n^=n>>33; return size_t(n);
    }
};

class saber_trail_history {
    struct sample {
        double time=0.0;
        saber_trail_vec3 direction{};
    };
    struct state {
        std::array<sample,64> samples{};
        unsigned count=0;
        bool armed=false;
        double last_active=0.0;
        double last_time=-1.0;
        saber_trail_vec3 base{};
    };
    std::unordered_map<saber_trail_key,state,saber_trail_key_hash> histories;
    uintptr_t world=0;

public:
    void clear() { histories.clear(); world=0; }
    void set_world(uintptr_t current) {
        if(world!=current) {
            histories.clear();
            world=current;
        }
    }

    // Called once per rendered blade. "active" follows JK's saber-damage
    // marker state. After marker 5, continue inserting CURRENT directions
    // for 'delay' seconds: the historical tip catches up, shrinking the fan.
    bool advance(saber_trail_key key,double now,bool active,
                 saber_trail_vec3 const &base,saber_trail_vec3 const &tip,
                 double delay,saber_trail_vec3 &delayed_direction)
    {
        if(!key.owner || !std::isfinite(now) || !std::isfinite(delay) ||
           delay<0.005 || delay>0.25)return false;
        saber_trail_vec3 direction{};
        if(!saber_trail_direction(base,tip,direction))return false;
        auto it=histories.find(key);
        if(it==histories.end()) {
            if(!active)return false;
            if(histories.size()>=256U) histories.clear(); // finite per-world bound
            it=histories.emplace(key,state{}).first;
        }
        auto &s=it->second;
        double distance2=0.0;
        for(int i=0;i<3;++i)
            distance2+=double(base[i]-s.base[i])*(base[i]-s.base[i]);
        // A teleport/long missed draw must not leave a huge disconnected fan.
        if(s.last_time>=0.0 &&
           (now<s.last_time || now-s.last_time>0.35 || distance2>16.0))
            s=state{};
        if(active && !s.armed) {
            s=state{};
            s.armed=true;
        }
        if(!s.armed) { histories.erase(it); return false; }
        if(active)s.last_active=now;
        else if(now-s.last_active>=delay) {
            histories.erase(it);return false;
        }
        s.base=base;
        s.last_time=now;
        if(s.count==s.samples.size()) {
            for(unsigned i=1;i<s.count;++i)s.samples[i-1]=s.samples[i];
            --s.count;
        }
        s.samples[s.count++]={now,direction};
        if(s.count<2U)return false;

        // Interpolate at an actual time offset, not a fixed number of frames.
        // Before the first delayed timestamp, use the initial pose so the
        // fan grows smoothly instead of appearing at its full extent.
        double const target=now-delay;
        saber_trail_vec3 past=s.samples[0].direction;
        for(unsigned i=1;i<s.count;++i) {
            if(s.samples[i].time<target) {
                past=s.samples[i].direction;
                continue;
            }
            double const a=s.samples[i-1].time,b=s.samples[i].time;
            float const f=b>a ? float(std::clamp((target-a)/(b-a),0.0,1.0)) : 1.f;
            for(int n=0;n<3;++n)
                past[n]=s.samples[i-1].direction[n]*(1.f-f)+
                         s.samples[i].direction[n]*f;
            float mag2=0.0f;
            for(float v:past)mag2+=v*v;
            if(mag2<1.e-8f)return false;
            float const inv=1.f/std::sqrt(mag2);
            for(float &v:past)v*=inv;
            break;
        }
        float dot=0.0f;
        for(int i=0;i<3;++i) dot+=direction[i]*past[i];
        if(dot>0.99995f)return false; // no wedge at rest or below ~0.6 degrees
        delayed_direction=past;
        return true;
    }
};
} // namespace jkgm
