#pragma once
// Orthographic directional-sun shadow projection math.
// Pure CPU helpers shared by the depth pass and deferred lighting pass.
#include "spotlight_shadow.hpp"
#include "sun.hpp"
#include <algorithm>
#include <array>
#include <cmath>
#include <limits>
#include <vector>

namespace jkgm {

struct sun_shadow_projection {
    decal_vec3 origin{},right{},up{},forward{};
    float width=1.0f,height=1.0f,range=1.0f;
};

struct sun_shadow_clip_point {
    float x=0.0f,y=0.0f,z=0.0f;
};

inline decal_vec3 sun_shadow_cross(decal_vec3 const &a,decal_vec3 const &b)
{
    return {{a[1]*b[2]-a[2]*b[1],
             a[2]*b[0]-a[0]*b[2],
             a[0]*b[1]-a[1]*b[0]}};
}

inline bool make_sun_shadow_projection(
    decal_world_geometry const &world,
    std::array<float,3> const &direction_toward_sun,
    sun_shadow_projection &out,
    std::vector<decal_volume_aabb> const &model_bounds={})
{
    if(!world.valid_world || (world.receivers.empty() && model_bounds.empty()) ||
       !std::isfinite(direction_toward_sun[0]) ||
       !std::isfinite(direction_toward_sun[1]) ||
       !std::isfinite(direction_toward_sun[2]))return false;

    auto const sun=normalized_sun_direction(direction_toward_sun);
    auto forward=decal_vec3{{-sun[0],-sun[1],-sun[2]}};
    decal_vec3 reference_up = std::fabs(forward[2])>0.95f
        ? decal_vec3{{0.0f,1.0f,0.0f}}
        : decal_vec3{{0.0f,0.0f,1.0f}};
    auto right=sun_shadow_cross(reference_up,forward);
    float const right_len=std::sqrt(decal_dot(right,right));
    if(!(right_len>1.0e-6f) || !std::isfinite(right_len))return false;
    for(float &v:right)v/=right_len;
    auto up=sun_shadow_cross(forward,right);

    float min_x=std::numeric_limits<float>::max();
    float min_y=min_x,min_z=min_x;
    float max_x=-min_x,max_y=-min_x,max_z=-min_x;
    bool found=false;
    auto include=[&](decal_vec3 const &point) {
            float const x=decal_dot(point,right);
            float const y=decal_dot(point,up);
            float const z=decal_dot(point,forward);
            min_x=std::min(min_x,x);max_x=std::max(max_x,x);
            min_y=std::min(min_y,y);max_y=std::max(max_y,y);
            min_z=std::min(min_z,z);max_z=std::max(max_z,z);
            found=true;
    };
    for(auto const &surface:world.receivers) {
        for(auto const &point:surface.polygon) {
            if(!decal_finite(point))return false;
            include(point);
        }
    }
    for(auto const &bounds:model_bounds) {
        if(!decal_finite(bounds.low) || !decal_finite(bounds.high))return false;
        for(int corner=0;corner<8;++corner) {
            decal_vec3 point{{(corner&1)?bounds.high[0]:bounds.low[0],
                              (corner&2)?bounds.high[1]:bounds.low[1],
                              (corner&4)?bounds.high[2]:bounds.low[2]}};
            include(point);
        }
    }
    if(!found)return false;

    float const raw_width=max_x-min_x;
    float const raw_height=max_y-min_y;
    float const raw_range=max_z-min_z;
    if(!(std::isfinite(raw_width)&&std::isfinite(raw_height)&&
         std::isfinite(raw_range)) || raw_width<0.0f ||
       raw_height<0.0f || raw_range<0.0f)return false;
    // A small perimeter protects edge receivers from clipping. The map spans
    // the full loaded level, so it remains stable as the camera moves.
    float const margin=std::max(1.0f,
        std::max(raw_width,std::max(raw_height,raw_range))*0.02f);
    out.right=right;out.up=up;out.forward=forward;
    out.origin={{right[0]*(min_x-margin)+up[0]*(min_y-margin)+
                     forward[0]*(min_z-margin),
                 right[1]*(min_x-margin)+up[1]*(min_y-margin)+
                     forward[1]*(min_z-margin),
                 right[2]*(min_x-margin)+up[2]*(min_y-margin)+
                     forward[2]*(min_z-margin)}};
    out.width=raw_width+2.0f*margin;
    out.height=raw_height+2.0f*margin;
    out.range=raw_range+2.0f*margin;
    return std::isfinite(out.width)&&std::isfinite(out.height)&&
           std::isfinite(out.range);
}

inline sun_shadow_clip_point sun_shadow_clip(
    sun_shadow_projection const &p,decal_vec3 const &world)
{
    auto const relative=decal_sub(world,p.origin);
    return {
        2.0f*decal_dot(relative,p.right)/p.width-1.0f,
        2.0f*decal_dot(relative,p.up)/p.height-1.0f,
        2.0f*decal_dot(relative,p.forward)/p.range-1.0f
    };
}

inline decal_volume_aabb sun_shadow_volume_bounds(
    sun_shadow_projection const &p)
{
    decal_volume_aabb bounds{};
    for(int axis=0;axis<3;++axis) {
        float const center=p.origin[axis]+
            p.right[axis]*p.width*0.5f+
            p.up[axis]*p.height*0.5f+
            p.forward[axis]*p.range*0.5f;
        float const extent=std::fabs(p.right[axis])*p.width*0.5f+
            std::fabs(p.up[axis])*p.height*0.5f+
            std::fabs(p.forward[axis])*p.range*0.5f;
        bounds.low[axis]=center-extent;
        bounds.high[axis]=center+extent;
    }
    return bounds;
}

inline bool sun_shadow_world_face_visible(
    uint32_t geometry_mode,decal_vec3 const &normal,
    std::array<float,3> const &direction_toward_sun)
{
    if(geometry_mode<2U || !decal_finite(normal))return false;
    return normal[0]*direction_toward_sun[0]+
           normal[1]*direction_toward_sun[1]+
           normal[2]*direction_toward_sun[2]>0.005f;
}

} // namespace jkgm
