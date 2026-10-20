#pragma once
// JK23 rdSprite resource snapshot: preserve the real SPR's MAT and its
// texel-center texture coordinates, instead of creating new decal textures.
// The resource is read-only and must be checked against native addresses.
#include "sprite_decal_gpu_mesh.hpp"
#include <cstdint>
#include <cstring>
#include <cmath>

namespace jkgm {
struct sprite_decal_resource {
    uintptr_t material=0;
    sprite_decal_texture_coords texcoords{};
};
inline bool read_sprite_decal_resource(
    decal_native_read const &read,uintptr_t sprite,
    sprite_decal_resource &out)
{
    if(!read || !decal_native_address_ok(sprite))return false;
    // rdSprite has embedded rdFace at +0x5c, material at rdFace+0x20,
    // and a pointer to the four rdVector2 UVs at sprite+0x9c.
    uint32_t material=0,uv_ptr=0,face_mode=0;
    if(!read_decal_u32(read,sprite+0x7c,material) ||
       !read_decal_u32(read,sprite+0x9c,uv_ptr) ||
       !read_decal_u32(read,sprite+0x64,face_mode) ||
       !decal_native_address_ok(material) ||
       !decal_native_address_ok(uv_ptr) || face_mode<2U)
        return false;
    float uv[8]{};
    if(!read(uv_ptr,uv,sizeof(uv)))return false;
    // 0: left/bottom, 1: right/bottom, 2: right/top, 3: left/top.
    sprite_decal_texture_coords tex{
        uv[0],uv[2],uv[5],uv[1]};
    if(!valid_sprite_decal_texture_coords(tex) ||
       std::fabs(uv[0]-uv[6])>0.001f ||
       std::fabs(uv[2]-uv[4])>0.001f ||
       std::fabs(uv[1]-uv[3])>0.001f ||
       std::fabs(uv[5]-uv[7])>0.001f)
        return false;
    out={uintptr_t(material),tex};
    return true;
}
}
