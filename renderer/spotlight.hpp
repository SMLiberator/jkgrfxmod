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

// Independent projected diffuse illumination and additive glow strengths.
// The current visual defaults preserve the prototype's 1.0 / 0.12 mix.
struct spotlight_intensities {
    float light=1.0f,add=0.12f;
};
inline bool spotlight_intensities_valid(spotlight_intensities const &v)
{
    return std::isfinite(v.light)&&std::isfinite(v.add) &&
           v.light>=0.0f&&v.add>=0.0f &&
           v.light<=16.0f&&v.add<=16.0f;
}
inline bool spotlight_fade_valid(float seconds)
{
    return std::isfinite(seconds)&&seconds>=0.0f&&seconds<=3600.0f;
}

struct spotlight_request {
    uintptr_t thing=0;
    uint32_t identity=0;
    uint32_t material=0; // stable native JK MAT index, not D3D handle
    float width=1.0f,height=1.0f,range=8.0f; // far-plane size and depth in JK units
    thing_node_vector offset{{0,0,0}};
    spotlight_intensities fade_from{},fade_to{};
    uint32_t fade_started_ms=0,fade_duration_ms=0;
};

inline spotlight_intensities spotlight_sample_intensities(
    spotlight_request const &request,uint32_t now_ms)
{
    if(request.fade_duration_ms==0)return request.fade_to;
    // Unsigned elapsed time is safe across GetTickCount's 32-bit rollover.
    float const t=std::min(1.0f,float(uint32_t(now_ms-request.fade_started_ms))/
                                    float(request.fade_duration_ms));
    return {
        request.fade_from.light+
          (request.fade_to.light-request.fade_from.light)*t,
        request.fade_from.add+
          (request.fade_to.add-request.fade_from.add)*t
    };
}

struct spotlight_instance {
    uint32_t material=0;
    float width=1.0f,height=1.0f,range=8.0f;
    float light_intensity=1.0f,add_intensity=0.12f;
    thing_node_vector origin{},right{},up{},forward{};
};

    
// Conservative footprint of a finite rectangular spotlight pyramid in screen
// UV coordinates (bottom-left origin). An invalid camera returns full-screen:
// optimization must never clip an otherwise valid spotlight.
struct spotlight_screen_rect {
    float min_x=0.0f,min_y=0.0f,max_x=1.0f,max_y=1.0f;
    bool visible=true;
};
struct spotlight_projection_camera {
    thing_node_vector position{},right{},up{},forward{};
    float tan_half_horizontal=1.0f,tan_half_vertical=1.0f;
    int width=1,height=1;
};
inline spotlight_screen_rect spotlight_screen_coverage(
    spotlight_instance const &spot,spotlight_projection_camera const &cam)
{
    spotlight_screen_rect fallback{};
    if(cam.width<=0 || cam.height<=0 ||
       !std::isfinite(cam.tan_half_horizontal) ||
       !std::isfinite(cam.tan_half_vertical) ||
       cam.tan_half_horizontal<=0.0f || cam.tan_half_vertical<=0.0f)
        return fallback;
    auto dot=[](thing_node_vector const &a,thing_node_vector const &b) {
        return a[0]*b[0]+a[1]*b[1]+a[2]*b[2];
    };
    // Vertices of the projector's exact square pyramid: the apex and four
    // far-plane corners. It matches the shader's UV limits and depth cutoff.
    std::array<thing_node_vector,5> vertices{};
    vertices[0]=spot.origin;
    for(int k=0;k<4;k++) {
        auto &p=vertices[k+1];
        float const sx=(k&1)?0.5f:-0.5f;
        float const sy=(k&2)?0.5f:-0.5f;
        for(int axis=0;axis<3;axis++)
            p[axis]=spot.origin[axis]+spot.forward[axis]*spot.range+
                    spot.right[axis]*spot.width*sx+
                    spot.up[axis]*spot.height*sy;
    }
    std::array<thing_node_vector,5> relative{};
    std::array<float,5> depths{};
    constexpr float near_plane=1.e-4f;
    for(int k=0;k<5;k++) {
        for(int axis=0;axis<3;axis++)
            relative[k][axis]=vertices[k][axis]-cam.position[axis];
        depths[k]=dot(relative[k],cam.forward);
        if(!std::isfinite(depths[k]))return fallback;
    }
    float lo_x=1.e30f,lo_y=1.e30f,hi_x=-1.e30f,hi_y=-1.e30f;
    bool found=false;
    auto project=[&](thing_node_vector const &rel,float depth)->bool {
        if(depth<near_plane)return true;
        float const x=0.5f+0.5f*dot(rel,cam.right)/
                             (depth*cam.tan_half_horizontal);
        float const y=0.5f+0.5f*dot(rel,cam.up)/
                             (depth*cam.tan_half_vertical);
        if(!std::isfinite(x)||!std::isfinite(y))return false;
        lo_x=std::min(lo_x,x);hi_x=std::max(hi_x,x);
        lo_y=std::min(lo_y,y);hi_y=std::max(hi_y,y);
        found=true;
        return true;
    };
    for(int k=0;k<5;k++)
        if(depths[k]>=near_plane && !project(relative[k],depths[k]))
            return fallback;
    // Clip pyramid edges against the camera-facing near plane. Projection
    // of a clipped convex polytope reaches its screen extrema at vertices,
    // so this includes even beams that cross the viewer's near plane.
    constexpr std::array<std::array<int,2>,8> edges{{
        {{0,1}},{{0,2}},{{0,3}},{{0,4}},
        {{1,2}},{{2,4}},{{4,3}},{{3,1}}
    }};
    for(auto const &edge:edges) {
        int const a=edge[0],b=edge[1];
        if((depths[a]<near_plane)==(depths[b]<near_plane))continue;
        float const t=(near_plane-depths[a])/(depths[b]-depths[a]);
        thing_node_vector intersection{};
        for(int axis=0;axis<3;axis++)
            intersection[axis]=relative[a][axis]+
                t*(relative[b][axis]-relative[a][axis]);
        if(!project(intersection,near_plane))return fallback;
    }
    if(!found)return {0.0f,0.0f,0.0f,0.0f,false};
    // Expand by two pixels to cover numerical roundoff, MSAA/depth-edge
    // reconstruction and fractional viewport positions.
    float const pad_x=2.0f/float(cam.width);
    float const pad_y=2.0f/float(cam.height);
    if(hi_x+pad_x<0.0f || lo_x-pad_x>1.0f ||
       hi_y+pad_y<0.0f || lo_y-pad_y>1.0f)
        return {0.0f,0.0f,0.0f,0.0f,false};
    return {std::max(0.0f,lo_x-pad_x),std::max(0.0f,lo_y-pad_y),
            std::min(1.0f,hi_x+pad_x),std::min(1.0f,hi_y+pad_y),true};
}

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
    return spotlight_intensities_valid(v.fade_from) &&
           spotlight_intensities_valid(v.fade_to);
}

inline bool spotlight_make_instance(spotlight_request const &request,
                                    thing_node_pose const &pose,
                                    spotlight_instance &out,
                                    uint32_t now_ms=0)
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
    auto const intensities=spotlight_sample_intensities(request,now_ms);
    out.light_intensity=intensities.light;
    out.add_intensity=intensities.add;
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
        spotlight_request updated=v;
        auto it=lights_.find(v.thing);
        if(it!=lights_.end() && it->second.identity==v.identity) {
            // Reassigning the MAT/size must not interrupt a fade.
            updated.fade_from=it->second.fade_from;
            updated.fade_to=it->second.fade_to;
            updated.fade_started_ms=it->second.fade_started_ms;
            updated.fade_duration_ms=it->second.fade_duration_ms;
        }
        lights_[v.thing]=updated;
        return true;
    }
    bool set_intensities(uintptr_t thing,uint32_t identity,
                         spotlight_intensities to,float seconds,
                         uint32_t now_ms)
    {
        if(!spotlight_intensities_valid(to) ||
           !spotlight_fade_valid(seconds))return false;
        std::lock_guard<std::mutex> guard(mutex_);
        auto it=lights_.find(thing);
        if(it==lights_.end() || it->second.identity!=identity)return false;
        auto &v=it->second;
        v.fade_from=spotlight_sample_intensities(v,now_ms);
        v.fade_to=to;
        v.fade_started_ms=now_ms;
        v.fade_duration_ms=static_cast<uint32_t>(seconds*1000.0f+0.5f);
        return true;
    }
    bool erase_thing(uintptr_t thing)
    {
        std::lock_guard<std::mutex> guard(mutex_);
        return lights_.erase(thing)!=0U;
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
