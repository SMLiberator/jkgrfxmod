#pragma once
// COG-controlled MAT spotlights. Each Thing owns at most one projector.
// The source Thing can itself be attached to an animated model node.
// A missing texture or destroyed Thing suppresses that frame.
#include "thing_node_math.hpp"
#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <map>
#include <mutex>
#include <vector>

namespace jkgm {
constexpr size_t spotlight_max_active=4;

struct spotlight_request {
    uintptr_t thing=0;
    uint32_t identity=0;
    uint32_t material=0; // stable native JK MAT index, not D3D handle
    float width=1.0f,height=1.0f,range=8.0f; // far-plane size and depth in JK units
    thing_node_vector offset{{0,0,0}};
};

struct spotlight_instance {
    uint32_t material=0;
    float width=1.0f,height=1.0f,range=8.0f;
    thing_node_vector origin{},right{},up{},forward{};
};

inline bool spotlight_request_valid(spotlight_request const &v)
{
    if(v.thing<0x10000U ||
       (v.material&0x7fffU)>=16384U ||
       (v.material&0xffff0000U)!=0 ||
       !std::isfinite(v.width)||!std::isfinite(v.height)||
       !std::isfinite(v.range)||
       v.width<=0.0f||v.height<=0.0f||v.range<=0.0f||
       v.width>256.0f||v.height>256.0f||v.range>256.0f)return false;
    for(float x:v.offset)if(!std::isfinite(x))return false;
    return true;
}

inline bool spotlight_make_instance(spotlight_request const &request,
                                    thing_node_pose const &pose,
                                    spotlight_instance &out)
{
    if(!spotlight_request_valid(request))return false;
    auto normalize=[](thing_node_vector &v)->bool {
        float const n2=v[0]*v[0]+v[1]*v[1]+v[2]*v[2];
        if(!std::isfinite(n2)||n2<1.e-10f)return false;
        float const inv=1.0f/std::sqrt(n2);
        for(float &x:v)x*=inv;
        return true;
    };
    auto right=pose.right,up=pose.up,forward=pose.look;
    if(!normalize(right)||!normalize(up)||!normalize(forward))return false;
    // Retain native right/look/up orientation (including mirroring).
    // Reject nonorthogonal bases rather than projecting malformed beams.
    auto dot=[](thing_node_vector a,thing_node_vector b) {
        return a[0]*b[0]+a[1]*b[1]+a[2]*b[2];
    };
    if(std::fabs(dot(right,up))>0.1f ||
       std::fabs(dot(right,forward))>0.1f ||
       std::fabs(dot(up,forward))>0.1f)return false;
    out.material=request.material;
    out.width=request.width;out.height=request.height;
    out.range=request.range;
    out.right=right;out.up=up;out.forward=forward;
    out.origin=pose.origin;
    for(int i=0;i<3;i++) {
        if(!std::isfinite(out.origin[i]))return false;
        out.origin[i]+=pose.right[i]*request.offset[0]+
                       pose.look[i]*request.offset[1]+
                       pose.up[i]*request.offset[2];
        if(!std::isfinite(out.origin[i]))return false;
    }
    return true;
}

class spotlight_registry {
    std::mutex mutex_;
    std::map<uintptr_t,spotlight_request> lights_;
    uint32_t world_=0;
public:
    void prepare_world(uint32_t world)
    {
        if(!world)return;
        std::lock_guard<std::mutex> guard(mutex_);
        if(world_ && world_!=world)lights_.clear();
        world_=world;
    }
    bool set(spotlight_request const &v)
    {
        if(!spotlight_request_valid(v))return false;
        std::lock_guard<std::mutex> guard(mutex_);
        if(lights_.size()>=spotlight_max_active &&
           !lights_.count(v.thing))return false;
        lights_[v.thing]=v;
        return true;
    }
    void erase_thing(uintptr_t thing)
    {
        std::lock_guard<std::mutex> guard(mutex_);
        lights_.erase(thing);
    }
    std::vector<spotlight_request> snapshot()
    {
        std::lock_guard<std::mutex> guard(mutex_);
        std::vector<spotlight_request> result;
        result.reserve(lights_.size());
        for(auto const &entry:lights_)result.push_back(entry.second);
        return result;
    }
};

inline spotlight_registry &get_spotlight_registry()
{
    static spotlight_registry rv;
    return rv;
}

// Defined in main.cpp; samples the owning Thing's world-space R/L/U.
std::vector<spotlight_instance> collect_spotlights_for_frame();
} // namespace jkgm
