#pragma once
// Converts a type-3 SPR Thing's world transform, offset and animated instance
// size/roll to the finite projector consumed by the surface decal clipper.
// Geometry depth deliberately remains one fixed engine-unit value for now.
#include "sprite_decal_clip.hpp"
#include "sprite_orientation.hpp"
#include <array>
#include <cmath>

namespace jkgm {
constexpr float sprite_decal_projection_depth=0.25f;
constexpr float sprite_decal_surface_bias=0.001f;

inline bool make_sprite_decal_projector(
    sprite_axes axes,
    decal_vec3 world_position,
    decal_vec3 local_spr_offset,
    float source_width,float source_height,
    float scale_width,float scale_height,
    float roll_degrees,
    decal_projector &out)
{
    if(!decal_finite(world_position)||!decal_finite(local_spr_offset)||
       !std::isfinite(source_width)||!std::isfinite(source_height)||
       !std::isfinite(scale_width)||!std::isfinite(scale_height)||
       !std::isfinite(roll_degrees) ||
       source_width<=0 || source_height<=0 ||
       scale_width<=0 || scale_height<=0 ||
       std::fabs(roll_degrees)>36000) return false;
    sprite_axes normalized{};
    if(!compute_sprite_axes(sprite_project_decal,axes,axes,normalized))
        return false;
    decal_vec3 position=world_position;
    for(int axis=0;axis<3;++axis)
        position[axis]+=local_spr_offset[0]*normalized.right[axis]+
                        local_spr_offset[1]*normalized.look[axis]+
                        local_spr_offset[2]*normalized.up[axis];
    float const angle=std::fmod(roll_degrees,360.0f)*
                      0.01745329251994329577f;
    float const ca=std::cos(angle),sa=std::sin(angle);
    decal_projector projector{};
    projector.origin=position;
    projector.look=normalized.look;
    for(int axis=0;axis<3;++axis) {
        projector.right[axis]=ca*normalized.right[axis]+
                              sa*normalized.up[axis];
        projector.up[axis]=ca*normalized.up[axis]-
                           sa*normalized.right[axis];
    }
    projector.width=source_width*scale_width;
    projector.height=source_height*scale_height;
    projector.depth=sprite_decal_projection_depth;
    if(!valid_decal_projector(projector))return false;
    out=projector;
    return true;
}

// Move the clipped triangle *outward* by a fixed tiny amount to avoid
// z-fighting. The original UV/depth projector coordinates are unchanged.
inline void offset_sprite_decal_receiver(
    decal_vertex &v,decal_vec3 receiver_normal)
{
    if(!decal_finite(receiver_normal))return;
    float const l2=decal_dot(receiver_normal,receiver_normal);
    if(l2<1.e-8f || !std::isfinite(l2))return;
    float const scale=sprite_decal_surface_bias/std::sqrt(l2);
    for(int i=0;i<3;++i)
        v.world[i]+=receiver_normal[i]*scale;
}
}
