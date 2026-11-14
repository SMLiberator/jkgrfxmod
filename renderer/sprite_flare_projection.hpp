#pragma once
// CPU reference of game_trns_pass.frag's SPR 0x40 center reconstruction.
// For a native billboard, normalized UV (0.5,0.5) identifies the center
// even when the original quad has been clipped to the viewing frustum.
#include <array>
#include <cstddef>
#include <cmath>
#include <initializer_list>

namespace jkgm {
struct sprite_flare_center {
    std::array<float,2> pixel{{0.0f,0.0f}};
    float view_depth=0.0f;
};
// One deterministic center per native flare TRIANGLE, calculated once on
// the CPU from its three clipped/perspective-correct input vertices. The
// native shader previously extrapolated the UV center from fragment
// derivatives; those derivatives can disagree at primitive/clip boundaries
// and produce diagonal flare slices. Clip coordinates are (x,y,w) from
// d3dtl_to_point; native view depth is clip w.
struct sprite_flare_triangle_vertex {
    float clip_x=0,clip_y=0,view_depth=0,u=0,v=0;
};
inline bool reconstruct_sprite_flare_triangle_center(
    std::array<sprite_flare_triangle_vertex,3> const &vertices,
    std::array<float,2> viewport,sprite_flare_center &out)
{
    out={};
    if(!std::isfinite(viewport[0]) || !std::isfinite(viewport[1]) ||
       viewport[0]<=0.0f || viewport[1]<=0.0f)return false;
    std::array<double,3> invw{},du{},dv{},screen_x{},screen_y{};
    for(size_t i=0;i<3;++i) {
        auto const &p=vertices[i];
        if(!std::isfinite(p.clip_x) || !std::isfinite(p.clip_y) ||
           !std::isfinite(p.view_depth) || !std::isfinite(p.u) ||
           !std::isfinite(p.v) || p.view_depth<=0.0f)return false;
        invw[i]=1.0/double(p.view_depth);
        du[i]=(double(p.u)-0.5)*invw[i];
        dv[i]=(double(p.v)-0.5)*invw[i];
        screen_x[i]=double(p.clip_x)*invw[i];
        screen_y[i]=double(p.clip_y)*invw[i];
    }
    // Solve perspective-correct UV=0.5 on the projected triangle. The
    // same unclipped center is recovered from either triangle or an
    // arbitrary clipped subdivision of a single native billboard.
    double const a=du[1]-du[0],b=du[2]-du[0];
    double const c=dv[1]-dv[0],d=dv[2]-dv[0];
    double const determinant=a*d-b*c;
    if(!std::isfinite(determinant) ||
       std::fabs(determinant)<1.e-14)return false;
    double const w1=(-du[0]*d+b*dv[0])/determinant;
    double const w2=(-a*dv[0]+du[0]*c)/determinant;
    double const w0=1.0-w1-w2;
    double const invdepth=w0*invw[0]+w1*invw[1]+w2*invw[2];
    if(!std::isfinite(invdepth) || invdepth<=0.0)return false;
    double const ndcx=w0*screen_x[0]+w1*screen_x[1]+w2*screen_x[2];
    double const ndcy=w0*screen_y[0]+w1*screen_y[1]+w2*screen_y[2];
    double const x=(ndcx+1.0)*0.5*viewport[0];
    double const y=(ndcy+1.0)*0.5*viewport[1];
    double const depth=1.0/invdepth;
    if(!std::isfinite(x) || !std::isfinite(y) ||
       !std::isfinite(depth) ||
       x<0.0 || x>=viewport[0] || y<0.0 || y>=viewport[1] ||
       depth>65500.0)return false;
    out.pixel={{float(x),float(y)}};
    out.view_depth=float(depth);
    return true;
}

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
