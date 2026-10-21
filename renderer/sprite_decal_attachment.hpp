#pragma once
// Convert a native JK world/Thing-face attachment normal into the
// projected SPR's world-space orthonormal axes. An attached blood
// projectile need not be hand-rotated: its image plane is tangent to
// the contacted polygon, and projection faces INTO that polygon.
// No mutation of the native Thing orientation or attachment pointers.
#include "sprite_decal_projector.hpp"
namespace jkgm {

inline bool sprite_decal_attachment_axes(
    sprite_axes const &original,decal_vec3 face_normal,sprite_axes &out)
{
    if(!decal_finite(face_normal)||!sprite_normalize(face_normal))
        return false;
    out.look={{-face_normal[0],-face_normal[1],-face_normal[2]}};
    // Preserve the original sprite's rotation around its projection axis
    // as much as possible; try Up if Right happens to align with the normal.
    out.right=original.right;
    if(!sprite_project_orthogonal(out.right,face_normal)) {
        out.right=original.up;
        if(!sprite_project_orthogonal(out.right,face_normal)) {
            out.right=std::fabs(face_normal[2])<0.8f
                ? sprite_vec3{{0,0,1}} : sprite_vec3{{1,0,0}};
            if(!sprite_project_orthogonal(out.right,face_normal))
                return false;
        }
    }
    out.up=sprite_cross(out.right,out.look);
    return sprite_normalize(out.up);
}

inline void sprite_decal_center_around_attached_face(
    decal_projector &projector,decal_vec3 face_normal)
{
    // Position may lie ON a hit plane or a small distance behind it.
    // Center the default 0.25-unit projection volume on the attachment.
    // The mesh's final 0.001-unit offset remains receiver-normal based.
    for(int k=0;k<3;k++)
        projector.origin[k]+=face_normal[k]*(projector.depth*0.5f);
}
}
