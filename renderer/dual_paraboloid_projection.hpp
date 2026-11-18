#pragma once
// Fixed +Z/-Z dual-paraboloid environment mapping (JK world axes).
// Each disk extends five degrees past the horizon to support seam blending.
// Independent of the primary camera; used by both CPU tests and GPU probes.
#include <array>
#include <cmath>
#include <cstdint>
#include "engine_camera.hpp"

namespace jkgm {
constexpr float dual_paraboloid_overlap_sine=0.08715574274765817f;
constexpr float dual_paraboloid_disk_radius=1.0913085010692714f; // tan(47.5 deg)

struct dual_paraboloid_sample {
    std::array<float,2> uv{};
    bool covered=false;
};
inline dual_paraboloid_sample project_dual_paraboloid_direction(
    std::array<float,3> direction,unsigned hemisphere)
{
    dual_paraboloid_sample out{};
    if(hemisphere>1U)return out;
    float const len2=direction[0]*direction[0]+
                     direction[1]*direction[1]+
                     direction[2]*direction[2];
    if(!std::isfinite(len2)||len2<=1.e-12f)return out;
    float const scale=1.0f/std::sqrt(len2);
    for(float &x:direction)x*=scale;
    float const sign=hemisphere==0U?1.0f:-1.0f;
    float const denom=1.0f+sign*direction[2];
    if(denom<=0.0f)return out;
    float const x=direction[0]/denom;
    float const y=sign*direction[1]/denom;
    float const radius2=x*x+y*y;
    out.covered=radius2<=dual_paraboloid_disk_radius*
                dual_paraboloid_disk_radius+1.e-5f;
    float const inv=0.5f/dual_paraboloid_disk_radius;
    out.uv={{0.5f+x*inv,0.5f+y*inv}};
    return out;
}
inline std::array<float,3> unproject_dual_paraboloid_direction(
    std::array<float,2> uv,unsigned hemisphere)
{
    if(hemisphere>1U)return {{0.0f,0.0f,0.0f}};
    float const x=(uv[0]*2.0f-1.0f)*dual_paraboloid_disk_radius;
    float const y=(uv[1]*2.0f-1.0f)*dual_paraboloid_disk_radius;
    float const inv=1.0f/(1.0f+x*x+y*y);
    float const sign=hemisphere==0U?1.0f:-1.0f;
    return {{2.0f*x*inv,sign*2.0f*y*inv,
             sign*(1.0f-x*x-y*y)*inv}};
}
// Both hemisphere textures contribute around the horizon; no discontinuous
// choice of a different capture face at exactly Z=0.
inline float dual_paraboloid_upper_weight(float direction_z) {
    float const t=std::fmax(0.0f,std::fmin(1.0f,
        (direction_z+dual_paraboloid_overlap_sine)/
        (2.0f*dual_paraboloid_overlap_sine)));
    return t*t*(3.0f-2.0f*t);
}

inline engine_camera make_dual_paraboloid_camera(
    std::array<float,3> const &probe,unsigned hemisphere)
{
    engine_camera cam{};
    if(hemisphere>1U || !engine_camera_detail::finite(probe))
        return cam;
    float const sign=hemisphere==0U?1.0f:-1.0f;
    cam.position=probe;
    cam.right={{1.0f,0.0f,0.0f}};
    cam.forward={{0.0f,0.0f,sign}};
    cam.up={{0.0f,-sign,0.0f}};
    cam.fov=90.0f; // radial mapping happens in the geometry shader
    cam.valid=true;
    return cam;
}
// Spherical range test: don't use perspective-frustum culling for a full
// hemisphere. This rejects only boxes strictly beyond capture far distance.
inline bool dual_paraboloid_outside_distance(
    std::array<float,3> const &bmin,
    std::array<float,3> const &bmax,
    std::array<float,3> const &origin,float far_distance)
{
    if(!(std::isfinite(far_distance) && far_distance>0.0f))
        return false;
    float distance2=0.0f;
    for(int i=0;i<3;++i) {
        if(!std::isfinite(bmin[i]) || !std::isfinite(bmax[i]) ||
           !std::isfinite(origin[i]) || bmin[i]>bmax[i])return false;
        float const delta=origin[i]<bmin[i]?bmin[i]-origin[i]:
                          origin[i]>bmax[i]?origin[i]-bmax[i]:0.0f;
        distance2+=delta*delta;
    }
    return distance2>far_distance*far_distance;
}
} // namespace jkgm
