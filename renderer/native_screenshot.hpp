#pragma once
// The verified SHOT/JSHOT screenshot entry from jkgm(5).exe, whose SHA256 is
// afb4605c8259075c151ad6b034b98562be87ebe90369d4d9abaee09b396ed50f.
// This EXE calls 0x4025E0 at 0x402B6E on INPUT_FUNC_SCREENSHOT (40). The
// native function uses "JSHOT%03d.PCX" in software rendering and
// "JSHOT%03d.BMP" in hardware rendering. Do not use OpenJKDF2's differing
// SHOT%04d naming assumption as a guard.
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
constexpr uintptr_t jk_screenshot_pcx_push=0x00402668U;
constexpr uintptr_t jk_screenshot_bmp_push=0x0040273EU;
constexpr uintptr_t jk_screenshot_hotkey_call=0x00402B6EU;
constexpr uintptr_t jk_screenshot_pcx_format=0x005253A0U;
constexpr uintptr_t jk_screenshot_bmp_format=0x005253B4U;

inline bool screenshot_format(char const *actual,size_t len,
                              char const *expected)
{
    if(len<14U)return false;
    return std::memcmp(actual,expected,14U)==0;
}

template<typename Reader>
bool verified_jk_screenshot(Reader const &read,uintptr_t module_base)
{
    if(module_base!=jk_executable_base)return false;
    // Guard the real function prologue, both format references, both string
    // contents, and the screenshot hotkey's direct CALL. A different JK
    // executable must fall back to native behavior instead of detouring code
    // that merely happens to reside at the same address.
    std::array<uint8_t,8> start{};
    constexpr std::array<uint8_t,8> expected_start{{
        0x81,0xEC,0xB8,0x00,0x00,0x00,0x53,0x55
    }};
    if(!read(jk_game_screenshot_address,start.data(),start.size()) ||
       start!=expected_start)return false;

    auto verify_push=[&](uintptr_t instruction,uintptr_t pointer,
                         char const *format) {
        std::array<uint8_t,5> code{};
        if(!read(instruction,code.data(),code.size()) ||
           code[0]!=0x68)return false;
        uint32_t address=0;
        std::memcpy(&address,code.data()+1U,sizeof(address));
        if(address!=pointer)return false;
        std::array<char,14> name{};
        if(!read(pointer,name.data(),name.size()))return false;
        return screenshot_format(name.data(),name.size(),format);
    };
    if(!verify_push(jk_screenshot_pcx_push,jk_screenshot_pcx_format,
                    "JSHOT%03d.PCX") ||
       !verify_push(jk_screenshot_bmp_push,jk_screenshot_bmp_format,
                    "JSHOT%03d.BMP"))return false;

    std::array<uint8_t,5> call{};
    if(!read(jk_screenshot_hotkey_call,call.data(),call.size()) ||
       call[0]!=0xE8)return false;
    int32_t offset=0;
    std::memcpy(&offset,call.data()+1U,sizeof(offset));
    return int64_t(jk_screenshot_hotkey_call)+5+offset==
           int64_t(jk_game_screenshot_address);
}

// Preserve JKGM's "JSHOT" + 3 digits naming, now with PNG output.
inline std::string png_filename(unsigned index)
{
    char name[32]{};
    std::snprintf(name,sizeof(name),"JSHOT%03u.PNG",index);
    return name;
}

// glReadPixels returns bottom-to-top rows; PNG encoding needs top-down rows.
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

// The window may have undefined alpha despite its visibly opaque contents.
inline void force_opaque_alpha(uint8_t *pixels,size_t width,size_t height)
{
    if(!pixels || !width || !height)return;
    for(size_t i=0;i<width*height;++i)
        pixels[4U*i+3U]=255U;
}
} // namespace native_screenshot
} // namespace jkgm
