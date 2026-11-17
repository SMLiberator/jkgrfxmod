#pragma once
// One camera-facing environment capture for a single owning SithThing.
// This is a 160-degree PERSPECTIVE approximation to a hemisphere, not a
// nonlinear 180-degree fisheye. Directions beyond its image fall back to
// the material's explicitly authored EnvMap.
#include "planar_reflection_camera.hpp"
#include <array>
#include <cmath>

namespace jkgm {
inline engine_camera make_hemi_reflection_camera(
    std::array<float,3> const &probe,
    engine_camera const &viewer,float fov=160.0f)
{
    engine_camera out{};
    if(!viewer.valid || !engine_camera_detail::finite(probe) ||
       !engine_camera_detail::finite(viewer.position) ||
       !std::isfinite(fov) || fov<90.0f || fov>=179.0f)return out;
    auto const length=[](std::array<float,3> const &v) {
        return std::sqrt(engine_camera_detail::dot(v,v));
    };
    auto const cross=[](std::array<float,3> const &a,
                        std::array<float,3> const &b) {
        return std::array<float,3>{{
            a[1]*b[2]-a[2]*b[1],
            a[2]*b[0]-a[0]*b[2],
            a[0]*b[1]-a[1]*b[0]
        }};
    };
    std::array<float,3> forward{{
        viewer.position[0]-probe[0],
        viewer.position[1]-probe[1],
        viewer.position[2]-probe[2]
    }};
    float d=length(forward);
    if(!(d>0.0001f)&&engine_camera_detail::finite(viewer.forward)) {
        forward={{-viewer.forward[0],-viewer.forward[1],-viewer.forward[2]}};
        d=length(forward);
    }
    if(!(std::isfinite(d)&&d>0.0001f))return out;
    for(auto &a:forward)a/=d;
    std::array<float,3> reference{{0.0f,0.0f,1.0f}};
    // Prevent a near-vertical cross product from collapsing.
    if(std::fabs(engine_camera_detail::dot(forward,reference))>0.98f)
        reference={{1.0f,0.0f,0.0f}};
    auto right=cross(forward,reference);
    d=length(right);
    if(!(std::isfinite(d)&&d>0.0001f))return out;
    for(auto &a:right)a/=d;
    auto up=cross(right,forward);
    out.position=probe;
    out.right=right;
    out.forward=forward;
    out.up=up;
    out.fov=fov;
    out.valid=true;
    return out;
}

// Screen-facing approximate hemisphere projection in probe coordinates.
// The returned UV is valid only for directions inside the one perspective
// image; values outside remain uncovered and use the EnvMap fallback.
struct hemi_reflection_sample {
    std::array<float,2> uv{};
    bool covered=false;
};
inline hemi_reflection_sample project_hemi_reflection_direction(
    engine_camera const &probe_camera,std::array<float,3> direction,
    float aspect=1.0f)
{
    hemi_reflection_sample out{};
    if(!probe_camera.valid || !std::isfinite(aspect) || aspect<=0.0f ||
       !engine_camera_detail::finite(direction))return out;
    float const y=engine_camera_detail::dot(direction,probe_camera.forward);
    float const h=std::tan(probe_camera.fov*0.008726646259971648f);
    if(!(y>0.00001f && h>0.0f))return out;
    float const u=engine_camera_detail::dot(direction,probe_camera.right)/(y*h);
    float const v=engine_camera_detail::dot(direction,probe_camera.up)/(y*h/aspect);
    out.uv={{0.5f+0.5f*u,0.5f+0.5f*v}};
    out.covered=std::fabs(u)<1.0f && std::fabs(v)<1.0f;
    return out;
}


// Fixed world-axis cubemap orientation. Each square 90-degree perspective
// view captures one layer of a single GL_TEXTURE_2D_ARRAY. Unlike the hemi
// camera this orientation is independent of the viewer, avoiding rotation
// jitter as the player moves around the reflective Thing.
inline engine_camera make_cube_reflection_camera(
    std::array<float,3> const &probe,unsigned face)
{
    engine_camera out{};
    if(!engine_camera_detail::finite(probe) || face>=6U)return out;
    static constexpr std::array<std::array<float,3>,6> forward{{
        {{ 1.0f, 0.0f, 0.0f}}, {{-1.0f, 0.0f, 0.0f}},
        {{ 0.0f, 1.0f, 0.0f}}, {{ 0.0f,-1.0f, 0.0f}},
        {{ 0.0f, 0.0f, 1.0f}}, {{ 0.0f, 0.0f,-1.0f}}
    }};
    static constexpr std::array<std::array<float,3>,6> right{{
        {{ 0.0f,-1.0f, 0.0f}}, {{ 0.0f, 1.0f, 0.0f}},
        {{ 1.0f, 0.0f, 0.0f}}, {{-1.0f, 0.0f, 0.0f}},
        {{ 0.0f, 1.0f, 0.0f}}, {{ 0.0f,-1.0f, 0.0f}}
    }};
    static constexpr std::array<std::array<float,3>,6> up{{
        {{ 0.0f, 0.0f, 1.0f}}, {{ 0.0f, 0.0f, 1.0f}},
        {{ 0.0f, 0.0f, 1.0f}}, {{ 0.0f, 0.0f, 1.0f}},
        {{ 1.0f, 0.0f, 0.0f}}, {{ 1.0f, 0.0f, 0.0f}}
    }};
    out.position=probe;
    out.forward=forward[face];out.right=right[face];out.up=up[face];
    out.fov=90.0f;out.valid=true;
    return out;
}

struct cube_reflection_sample {
    std::array<float,2> uv{};
    unsigned face=0U;
    bool covered=false;
};
inline cube_reflection_sample project_cube_reflection_direction(
    std::array<float,3> const &probe,std::array<float,3> const &direction)
{
    cube_reflection_sample out{};
    if(!engine_camera_detail::finite(direction))return out;
    float const ax=std::fabs(direction[0]),ay=std::fabs(direction[1]),
                az=std::fabs(direction[2]);
    float major=0.0f;
    if(ax>=ay && ax>=az) {
        out.face=direction[0]>=0.0f?0U:1U;major=ax;
    } else if(ay>=az) {
        out.face=direction[1]>=0.0f?2U:3U;major=ay;
    } else {
        out.face=direction[2]>=0.0f?4U:5U;major=az;
    }
    if(!(major>0.0f))return out;
    auto const camera=make_cube_reflection_camera(probe,out.face);
    if(!camera.valid)return out;
    float const u=engine_camera_detail::dot(direction,camera.right)/major;
    float const v=engine_camera_detail::dot(direction,camera.up)/major;
    out.uv={{0.5f+0.5f*u,0.5f+0.5f*v}};
    out.covered=std::fabs(u)<=1.0001f && std::fabs(v)<=1.0001f;
    return out;
}
}
