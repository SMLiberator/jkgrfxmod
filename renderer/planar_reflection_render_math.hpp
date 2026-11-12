#pragma once
// GL-independent capture dimensions and reflection-side clipping plane.
#include "planar_reflection_camera.hpp"
#include <algorithm>
#include <array>
#include <cmath>
#include <limits>

namespace jkgm {
struct planar_reflection_render_setup {
    int width=0,height=0;
    // Plane equation in mirrored-camera (right/forward/up) coordinates.
    // The accepted half-space is the ORIGINAL observer's side of the plane.
    std::array<float,4> clip_view{};
    bool valid=false;
};

inline planar_reflection_render_setup make_planar_reflection_render_setup(
    int display_width,int display_height,float scale,
    engine_camera const &main_camera,
    engine_camera const &reflected_camera,
    planar_reflection_plane const &plane)
{
    planar_reflection_render_setup out{};
    if(display_width<1 || display_height<1 ||
       display_width>16384 || display_height>16384 ||
       !std::isfinite(scale) || scale<0.25f || scale>1.0f ||
       !main_camera.valid || !reflected_camera.valid)return out;
    planar_reflection_plane p{};
    if(!normalize_planar_reflection_plane(plane,p))return out;
    double const side=engine_camera_detail::dot(main_camera.position,
                                                 p.normal)-p.offset;
    // The capture lies on the opposite side; refuse a plane that passes
    // through the camera rather than choose an unstable clipping side.
    if(!std::isfinite(side) || std::fabs(side)<1e-4)return out;
    float const sign=side>0.0 ? 1.0f : -1.0f;
    double const w=engine_camera_detail::dot(reflected_camera.position,
                                              p.normal)-p.offset;
    out.clip_view={{
        sign*engine_camera_detail::dot(reflected_camera.right,p.normal),
        sign*engine_camera_detail::dot(reflected_camera.forward,p.normal),
        sign*engine_camera_detail::dot(reflected_camera.up,p.normal),
        float(sign*w)
    }};
    for(float v:out.clip_view)if(!std::isfinite(v))return {};
    out.width=std::max(1,int(std::ceil(double(display_width)*scale)));
    out.height=std::max(1,int(std::ceil(double(display_height)*scale)));
    out.valid=true;
    return out;
}
} // namespace jkgm
