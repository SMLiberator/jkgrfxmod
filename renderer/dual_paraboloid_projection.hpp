#pragma once
// Fixed +Z/-Z dual-paraboloid environment mapping (JK world axes).
// Each disk extends five degrees past the horizon to support seam blending.
// Independent of the primary camera; used by both CPU tests and GPU probes.
#include <array>
#include <cmath>
#include <cstdint>
#include "engine_camera.hpp"
#include "gpu_3do_modelview.hpp"
#include <algorithm>

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
// For a posed mesh or world surface, the same model-to-capture transform
// used by the capture shader defines the paraboloid camera's Y axis.
// The shader accepts p.y + sin(5deg)*length(p) >= 0. Bound the entire
// transformed AABB, preserving the 5-degree seam overlap. Invalid bounds
// always fail open rather than deleting visible reflection geometry.
inline bool dual_paraboloid_bounds_outside(
    gpu_3do_matrix34 const &to_capture,
    std::array<float,3> const &lo,std::array<float,3> const &hi,
    float far_distance)
{
    if(!gpu_3do_matrix_finite(to_capture) ||
       !std::isfinite(far_distance)||far_distance<=0.0f)return false;
    std::array<float,3> center{},half{};
    for(unsigned i=0;i<3U;++i) {
        if(!std::isfinite(lo[i])||!std::isfinite(hi[i])||lo[i]>hi[i])
            return false;
        center[i]=lo[i]*0.5f+hi[i]*0.5f;
        half[i]=(hi[i]-lo[i])*0.5f;
    }
    auto const p=gpu_3do_native_transform_point(to_capture,center);
    auto const &m=to_capture;
    std::array<double,3> extent{{
        double(std::fabs(m[0]))*half[0]+double(std::fabs(m[3]))*half[1]+double(std::fabs(m[6]))*half[2],
        double(std::fabs(m[1]))*half[0]+double(std::fabs(m[4]))*half[1]+double(std::fabs(m[7]))*half[2],
        double(std::fabs(m[2]))*half[0]+double(std::fabs(m[5]))*half[1]+double(std::fabs(m[8]))*half[2]}};
    double nearest2=0.0,max2=0.0;
    for(unsigned i=0;i<3U;++i) {
        if(!std::isfinite(p[i])||!std::isfinite(extent[i]))
            return false;
        double const magnitude=std::fabs(double(p[i]));
        double const near_axis=std::fmax(0.0,magnitude-extent[i]);
        double const far_axis=magnitude+extent[i];
        nearest2+=near_axis*near_axis;
        max2+=far_axis*far_axis;
    }
    if(!std::isfinite(nearest2)||!std::isfinite(max2))return false;
    double const max_capture_distance=double(far_distance)+0.02;
    if(nearest2>max_capture_distance*max_capture_distance)return true;
    // If even maximum Y and maximum possible radial distance cannot
    // satisfy the overlap-aware shader clip, the box is fully behind.
    return double(p[1])+extent[1]+
           double(dual_paraboloid_overlap_sine)*std::sqrt(max2)<-0.02;
}
inline bool dual_paraboloid_sphere_outside_distance(
    std::array<float,3> const &center,float radius,
    std::array<float,3> const &origin,float far_distance)
{
    if(!std::isfinite(radius)||radius<=0.0f ||
       !std::isfinite(far_distance)||far_distance<=0.0f)return false;
    double distance2=0.0;
    for(unsigned i=0;i<3U;++i) {
        if(!std::isfinite(center[i])||!std::isfinite(origin[i]))
            return false;
        double const d=double(center[i])-double(origin[i]);
        distance2+=d*d;
    }
    double const limit=double(far_distance)+double(radius)+0.02;
    return std::isfinite(distance2) && distance2>limit*limit;
}
} // namespace jkgm
