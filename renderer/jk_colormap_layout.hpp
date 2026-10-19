#pragma once
// rdColormap layout in the tested JK23 RGB/smooth-normals executable.
// This build includes SITH_DEBUG_STRUCT_NAMES' 32-byte colormap name,
// shifting every field by 0x20 relative to the retail OpenJKDF2 layout.
// Confirmed from a live CMP #0 dump: flags=1 at +0x20, three tint floats
// (1,1,1) at +0x24, and black/green/red palette entries at +0x30.
#include <cstddef>
#include <cstdint>

namespace jkgm {
struct jk_rd_colormap_palette_prefix {
    char debug_name[32];
    uint32_t flags;
    float tint[3];
    uint8_t colors[256 * 3];
};
constexpr size_t jk_cmp_flags_offset =
    offsetof(jk_rd_colormap_palette_prefix,flags);
constexpr size_t jk_cmp_tint_offset =
    offsetof(jk_rd_colormap_palette_prefix,tint);
constexpr size_t jk_cmp_colors_offset =
    offsetof(jk_rd_colormap_palette_prefix,colors);
static_assert(jk_cmp_flags_offset==0x20,"JK23 CMP flags offset changed");
static_assert(jk_cmp_tint_offset==0x24,"JK23 CMP tint offset changed");
static_assert(jk_cmp_colors_offset==0x30,"JK23 CMP RGB offset changed");
}
