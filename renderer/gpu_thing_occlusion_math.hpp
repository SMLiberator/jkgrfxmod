#pragma once
// Model-level occlusion uses JK's *authored*, Thing-centered rdModel3 sphere.
// The Thing's animation NEVER changes this radius. These helpers intentionally
// do not inspect animated rdMesh bounds or pose data.
#include "engine_camera.hpp"
#include <array>
#include <cmath>
#include <cstdint>

namespace jkgm {
struct gpu_thing_occlusion_bound {
    std::array<float,3> center{};
    float radius=0.0f;
    bool valid=false;
};
inline bool gpu_thing_occlusion_bound_valid(gpu_thing_occlusion_bound const &b) {
    if(!b.valid || !std::isfinite(b.radius) || b.radius<=0.0f)return false;
    for(float f:b.center)if(!std::isfinite(f))return false;
    return true;
}
inline bool gpu_thing_occlusion_same_bound(
    gpu_thing_occlusion_bound const &a,
    gpu_thing_occlusion_bound const &b) {
    if(!gpu_thing_occlusion_bound_valid(a) ||
       !gpu_thing_occlusion_bound_valid(b) ||
       std::fabs(a.radius-b.radius)>0.01f)return false;
    float d2=0.0f;
    for(int i=0;i<3;++i) {
        float const d=a.center[i]-b.center[i];d2+=d*d;
    }
    return d2<0.0025f;
}
inline bool gpu_thing_occlusion_camera_inside(
    gpu_thing_occlusion_bound const &b,
    engine_camera const &c)
{
    if(!gpu_thing_occlusion_bound_valid(b)||!c.valid)return true;
    float d2=0.0f;
    for(int i=0;i<3;++i) {
        float const d=b.center[i]-c.position[i];
        if(!std::isfinite(d))return true;
        d2+=d*d;
    }
    float const r=b.radius+0.15f;
    return d2<=r*r;
}
inline bool gpu_thing_occlusion_camera_compatible(
    engine_camera const &a,engine_camera const &b)
{
    if(!a.valid || !b.valid || !std::isfinite(a.fov) ||
       !std::isfinite(b.fov) || std::fabs(a.fov-b.fov)>0.02f)return false;
    float moved=0.0f,rotated=0.0f;
    for(int i=0;i<3;++i) {
        float const d=a.position[i]-b.position[i];
        float const f=a.forward[i]-b.forward[i];
        float const u=a.up[i]-b.up[i];
        if(!std::isfinite(d)||!std::isfinite(f)||!std::isfinite(u))return false;
        moved+=d*d;rotated+=f*f+u*u;
    }
    return moved<=0.0625f && rotated<=0.001f;
}
} // namespace jkgm
