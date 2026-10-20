#pragma once
// CPU reference of game_trns_pass.frag's SPR 0x40 center reconstruction.
// For a native billboard, normalized UV (0.5,0.5) identifies the center
// even when the original quad has been clipped to the viewing frustum.
#include <array>
#include <cmath>
#include <initializer_list>

namespace jkgm {
struct sprite_flare_center {
    std::array<float,2> pixel{{0.0f,0.0f}};
    float view_depth=0.0f;
};
inline bool reconstruct_sprite_flare_center(
    std::array<float,2> fragment_pixel,
    std::array<float,2> fragment_uv,float fragment_depth,
    std::array<float,2> uv_dx,std::array<float,2> uv_dy,
    float depth_dx,float depth_dy,
    std::array<float,2> viewport,
    sprite_flare_center &out)
{
    for(float x:{fragment_pixel[0],fragment_pixel[1],
                 fragment_uv[0],fragment_uv[1],fragment_depth,
                 uv_dx[0],uv_dx[1],uv_dy[0],uv_dy[1],
                 depth_dx,depth_dy,viewport[0],viewport[1]})
        if(!std::isfinite(x))return false;
    float const det=uv_dx[0]*uv_dy[1]-uv_dx[1]*uv_dy[0];
    if(std::fabs(det)<1.e-8f) return false;
    float const u=0.5f-fragment_uv[0],v=0.5f-fragment_uv[1];
    float const x=(u*uv_dy[1]-v*uv_dy[0])/det;
    float const y=(uv_dx[0]*v-uv_dx[1]*u)/det;
    out.pixel={{fragment_pixel[0]+x,fragment_pixel[1]+y}};
    out.view_depth=fragment_depth+depth_dx*x+depth_dy*y;
    return out.pixel[0]>=0 && out.pixel[0]<viewport[0] &&
           out.pixel[1]>=0 && out.pixel[1]<viewport[1] &&
           out.view_depth>0 && std::isfinite(out.view_depth);
}
}
