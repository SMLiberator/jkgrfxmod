#pragma once
// Fixed-axis perspective cameras shared by CUBE and HEMI source captures.
// HEMI now samples two up/down paraboloid disks, not a camera-facing 160-degree
// perspective image. During validation six cube views feed the conversion.
#include "planar_reflection_camera.hpp"
#include "dual_paraboloid_projection.hpp"
#include <array>
#include <cmath>

namespace jkgm {
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
