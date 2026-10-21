#pragma once
// Convert a native JK world/Thing-face attachment normal into the
// projected SPR's world-space orthonormal axes. An attached blood
// projectile need not be hand-rotated: its UP axis matches the polygon
// normal, and the volume projects into the polygon (opposite UP).
// No mutation of the native Thing orientation or attachment pointers.
#include "sprite_decal_projector.hpp"
namespace jkgm {

inline bool sprite_decal_attachment_axes(
    sprite_axes const &original,decal_vec3 face_normal,sprite_axes &out)
{
    if(!decal_finite(face_normal)||!sprite_normalize(face_normal))
        return false;
    out.up=face_normal;
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
    out.look=sprite_cross(out.up,out.right);
    return sprite_normalize(out.look);
}

}
