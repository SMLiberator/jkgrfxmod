#pragma once
// JK's Screenshot hotkey and jkScreenshot COG verb both call jkGame_Screenshot.
// The original path reads a legacy DirectDraw backbuffer (magenta in JKGM).
// A detour may be installed only when the JKGM screenshot routine is verified:
// never trust OpenJKDF2's symbolic address alone on a modified executable.
#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <string>

namespace jkgm {
namespace native_screenshot {
constexpr uintptr_t jk_game_screenshot_address=0x004025E0U;
constexpr uintptr_t jk_executable_base=0x00400000U;
// Next function in the original JK layout: jkGame_ddraw_idk_palettes.
// This is merely a bounded search, not a guarantee that JKGM matches.
constexpr size_t signature_code_bytes=0x1E0U;

inline bool screenshot_format(char const *s,size_t len)
{
    if(len<13U)return false;
    auto lower=[](char c) {
        return c>='A'&&c<='Z'?char(c-'A'+'a'):c;
    };
    if(lower(s[0])!='s' || lower(s[1])!='h' ||
       lower(s[2])!='o' || lower(s[3])!='t')
        return false;
    // SHOT%04d.BMP is the native filename format. Require an exact
    // format prefix, not an arbitrary x86 pointer containing "SHOT".
    char const prefix[]="%04d.bmp";
    for(size_t i=0;i<sizeof(prefix)-1U;++i)
        if(lower(s[i+4U])!=prefix[i])return false;
    return true;
}

template<typename Reader>
bool verified_jk_screenshot(Reader const &read,uintptr_t module_base)
{
    if(module_base!=jk_executable_base)return false;
    std::array<uint8_t,signature_code_bytes> code{};
    if(!read(jk_game_screenshot_address,code.data(),code.size()))
        return false;
    // Avoid hooking an unrelated function at the upstream JK address.
    switch(code[0]) {
    case 0x55: case 0x53: case 0x56: case 0x57:
    case 0x81: case 0x83: case 0x8B: case 0xA1:
        break;
    default: return false;
    }
    // Look for a little-endian absolute reference from the function body
    // to the native SHOT%04d.BMP format string in readable module memory.
    // If JKGM moved the function, fail closed until its location is verified.
    for(size_t i=0;i+4U<=code.size();++i) {
        uint32_t straddr=0;
        std::memcpy(&straddr,code.data()+i,sizeof(straddr));
        if(straddr<jk_executable_base || straddr>=0x02000000U)
            continue;
        std::array<char,13> format{};
        if(!read(straddr,format.data(),format.size()))
            continue;
        if(screenshot_format(format.data(),format.size()))
            return true;
    }
    return false;
}

inline std::string png_filename(unsigned index)
{
    char name[32]{};
    std::snprintf(name,sizeof(name),"SHOT%04u.PNG",index);
    return name;
}

// glReadPixels returns bottom-to-top rows; stb_image_write emits exactly
// the supplied row order. Flip RGBA scanlines BEFORE the PNG encoder.
inline void flip_rgba_rows(uint8_t *pixels,size_t width,size_t height)
{
    if(!pixels || !width || !height)return;
    size_t const stride=width*4U;
    for(size_t y=0;y<height/2U;++y) {
        uint8_t *top=pixels+y*stride;
        uint8_t *bottom=pixels+(height-1U-y)*stride;
        for(size_t i=0;i<stride;++i)
            std::swap(top[i],bottom[i]);
    }
}
// Window backbuffers often have undefined/zero alpha even though they are
// visibly opaque. PNG alpha must NOT make the screenshot transparent.
inline void force_opaque_alpha(uint8_t *pixels,size_t width,size_t height)
{
    if(!pixels || !width || !height)return;
    for(size_t i=0;i<width*height;++i)
        pixels[4U*i+3U]=255U;
}
} // namespace native_screenshot
} // namespace jkgm
