#pragma once
// Model-level occlusion uses JK's *authored*, Thing-centered rdModel3 sphere.
// The Thing's animation NEVER changes this radius. These helpers intentionally
// do not inspect animated rdMesh bounds or pose data.
#include "engine_camera.hpp"
#include "gpu_3do_modelview.hpp"
#include <algorithm>
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
// Exact perspective silhouette bounds of a sphere, projected onto a
// camera-facing rectangle at the Thing CENTER depth. Unlike the 3D box
// this is intentionally aggressive: a nearer portion of the model can
// still be visible when the rectangle at center depth fails the depth test.
// The rectangle must nevertheless enclose the authored sphere's projection.
// Reject spheres crossing the camera/near plane instead of risking a bad quad.
struct gpu_thing_occlusion_screen_rect {
    float left=0.0f,right=0.0f,bottom=0.0f,top=0.0f;
    float center_forward=0.0f;
    bool valid=false;
};
inline bool gpu_thing_occlusion_project_sphere_rect(
    gpu_thing_occlusion_bound const &bound,
    gpu_3do_matrix34 const &view,float tan_half_fov,
    float aspect,gpu_thing_occlusion_screen_rect &out)
{
    out={};
    if(!gpu_thing_occlusion_bound_valid(bound) ||
       !std::isfinite(tan_half_fov) || tan_half_fov<=0.0f ||
       !std::isfinite(aspect) || aspect<=0.0f)
        return false;
    auto const axis=[&](int index)->double {
        return double(bound.center[0])*view[index]+
               double(bound.center[1])*view[index+1]+
               double(bound.center[2])*view[index+2]+
               double(view[9+index/3]);
    };
    double const x=axis(0), depth=axis(3), z=axis(6);
    double const r=bound.radius;
    // A center-depth rectangle cannot safely represent a sphere that
    // intersects the near plane or camera. Fail open in that case.
    if(!std::isfinite(x) || !std::isfinite(depth) ||
       !std::isfinite(z) || depth<=r+0.1)
        return false;
    double const divisor=depth*depth-r*r;
    if(!(divisor>0.0) || !std::isfinite(divisor))return false;
    auto const slopes=[&](double offset,double &lo,double &hi) {
        double const inside=offset*offset+divisor;
        if(!(inside>=0.0) || !std::isfinite(inside))return false;
        double const spread=r*std::sqrt(inside);
        lo=(offset*depth-spread)/divisor;
        hi=(offset*depth+spread)/divisor;
        return std::isfinite(lo) && std::isfinite(hi);
    };
    double xlo=0.0,xhi=0.0,zlo=0.0,zhi=0.0;
    if(!slopes(x,xlo,xhi) || !slopes(z,zlo,zhi))return false;
    double const h=tan_half_fov,v=h/aspect;
    xlo/=h;xhi/=h;zlo/=v;zhi/=v;
    if(xhi< -1.0 || xlo>1.0 || zhi< -1.0 || zlo>1.0)
        return false;
    out.left=float((std::max)(-1.0,xlo));
    out.right=float((std::min)(1.0,xhi));
    out.bottom=float((std::max)(-1.0,zlo));
    out.top=float((std::min)(1.0,zhi));
    out.center_forward=float(depth);
    out.valid=std::isfinite(out.center_forward) &&
              out.center_forward>0.0f &&
              out.right>out.left && out.top>out.bottom;
    return out.valid;
}
} // namespace jkgm
