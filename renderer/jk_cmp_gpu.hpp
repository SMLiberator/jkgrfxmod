#pragma once
// JKGM's rdColormap carries a 256-entry RGB palette and a 64x256
// software-lighting lookup table. Those are different from SithSector.tint.
#include "jk_colormap_layout.hpp"
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstring>
namespace jkgm {
constexpr size_t jk_cmp_lightlevel_offset=jk_cmp_colors_offset+256U*3U;
constexpr size_t jk_cmp_lightlevel_bytes=64U*256U;
static_assert(jk_cmp_lightlevel_offset==0x330U,
              "JKGM's named CMP lightlevel pointer moved");
using jk_cmp_lighttable=std::array<uint8_t,jk_cmp_lightlevel_bytes>;

inline bool jk_cmp_is_fullbright(jk_cmp_lighttable const &levels,uint8_t index) {
    // Detect actual unshaded entries, not particular palette-index ranges.
    // A constant remapping also counts: software shading never changes it.
    uint8_t const color=levels[index];
    for(size_t level=1;level<64U;++level)
        if(levels[level*256U+index]!=color)return false;
    return true;
}

template<class Read>
bool jk_cmp_read_tint(Read const &read,uint32_t address,
                      std::array<float,3> &tint)
{
    if(address<0x10000U || address>=0x7ff00000U ||
       !read(uintptr_t(address)+jk_cmp_tint_offset,
             tint.data(),sizeof(tint)))return false;
    for(float v:tint)if(!std::isfinite(v) || v<0.0f || v>16.0f)
        return false;
    return true;
}

template<class Read>
bool jk_cmp_read_lighttable(Read const &read,uint32_t address,
                            jk_cmp_lighttable &table)
{
    if(address<0x10000U || address>=0x7ff00000U)return false;
    uint32_t source=0;
    if(!read(uintptr_t(address)+jk_cmp_lightlevel_offset,
             &source,sizeof(source)) ||
       source<0x10000U || source>=0x7ff00000U)return false;
    return read(uintptr_t(source),table.data(),table.size());
}

template<class Read>
bool jk_sector_cmp_tint(Read const &read,uint32_t sector,
                         std::array<float,3> &out)
{
    if(sector<0x10000U || sector>=0x7ff00000U)return false;
    uint32_t cmp=0;
    if(!read(uintptr_t(sector)+0x0cU,&cmp,sizeof(cmp)))return false;
    return jk_cmp_read_tint(read,cmp,out);
}
}
