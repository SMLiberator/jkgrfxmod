#pragma once
// Preserve original indexed MAT texels before JK's hardware upload converts
// them to 16-bit RGB. Fullbright is derived from all 64 CMP light levels,
// never from the apparent brightness of an RGB color.
//
// tVBuffer prefix layout is studied from OpenJKDF2, but the native source
// pointer comes from JKGM's independently verified rdTexture structures.
#include "jk_cmp_gpu.hpp"
#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <vector>

namespace jkgm {
#pragma pack(push,4)
struct jk_indexed_mip_prefix {
    uint32_t locks[3];
    int32_t width,height;
    uint32_t size,row_size,row_width;
    uint32_t format[14]; // rdTexFormat: is16bit, bpp, ...
    uint32_t palette,texels;
};
#pragma pack(pop)
static_assert(offsetof(jk_indexed_mip_prefix,format)==0x20U,
              "Unexpected tVBuffer format offset");
static_assert(offsetof(jk_indexed_mip_prefix,texels)==0x5cU,
              "Unexpected tVBuffer pixel pointer offset");

// Return true only if at least one visible source texel is fullbright.
// RGBA is white/opaque for masked pixels and black/opaque otherwise.
// Transparent indices must never become luminous. Fail closed on any bad
// native pointer, dimensions, format, stride or source memory read.
template<class Read>
bool jk_cmp_build_fullbright_mask(
    Read const &read,uint32_t cmp,uint32_t mip_address,
    uint32_t width,uint32_t height,bool transparent,uint8_t transparent_index,
    std::vector<char> &rgba)
{
    rgba.clear();
    if(cmp<0x10000U || cmp>=0x7ff00000U ||
       mip_address<0x10000U || mip_address>=0x7ff00000U ||
       width==0U || height==0U || width>4096U || height>4096U)
        return false;
    jk_indexed_mip_prefix mip{};
    if(!read(uintptr_t(mip_address),&mip,sizeof(mip)) ||
       mip.width!=static_cast<int32_t>(width) ||
       mip.height!=static_cast<int32_t>(height) ||
       mip.format[0]!=0U || mip.format[1]!=8U ||
       mip.row_size<width || mip.row_size>65536U ||
       uint64_t(mip.row_size)*height>16U*1024U*1024U ||
       mip.texels<0x10000U || mip.texels>=0x7ff00000U)
        return false;

    jk_cmp_lighttable levels{};
    std::array<uint8_t,256U*3U> palette{};
    if(!jk_cmp_read_lighttable(read,cmp,levels) ||
       !read(uintptr_t(cmp)+jk_cmp_colors_offset,
             palette.data(),palette.size()))
        return false;
    std::array<bool,256U> is_fullbright{};
    bool any_index=false;
    for(unsigned i=0;i<256U;++i) {
        is_fullbright[i]=
            (!transparent || i!=transparent_index) &&
            jk_cmp_is_fullbright(levels,palette,static_cast<uint8_t>(i));
        any_index=any_index || is_fullbright[i];
    }
    if(!any_index)return false;

    size_t const source_bytes=size_t(mip.row_size)*height;
    std::vector<uint8_t> texels(source_bytes);
    // The game's read_bytes helper bounds each read to 1 MiB.
    for(size_t off=0;off<source_bytes;) {
        size_t const n=std::min<size_t>(source_bytes-off,1024U*1024U);
        if(!read(uintptr_t(mip.texels)+off,texels.data()+off,n))
            return false;
        off+=n;
    }
    rgba.resize(size_t(width)*height*4U,0);
    bool any_pixel=false;
    for(size_t y=0;y<height;++y)
        for(size_t x=0;x<width;++x) {
            bool const bright=is_fullbright[
                texels[y*size_t(mip.row_size)+x]];
            size_t const dest=(y*size_t(width)+x)*4U;
            char const value=bright?static_cast<char>(255):0;
            rgba[dest]=rgba[dest+1]=rgba[dest+2]=value;
            rgba[dest+3]=static_cast<char>(255);
            any_pixel=any_pixel || bright;
        }
    if(!any_pixel)rgba.clear();
    return any_pixel;
}
} // namespace jkgm
