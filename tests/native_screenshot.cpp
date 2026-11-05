#include "renderer/native_screenshot.hpp"
#include <array>
#include <cassert>
#include <cstdint>
#include <cstring>
#include <string>

int main()
{
    using namespace jkgm::native_screenshot;
    assert(png_filename(0U)=="JSHOT000.PNG");
    assert(png_filename(12U)=="JSHOT012.PNG");
    assert(png_filename(999U)=="JSHOT999.PNG");
    assert(screenshot_format("JSHOT%03d.BMP",14U,"JSHOT%03d.BMP"));
    assert(screenshot_format("JSHOT%03d.PCX",14U,"JSHOT%03d.PCX"));
    assert(!screenshot_format("SHOT%04d.BMP",13U,"JSHOT%03d.BMP"));
    assert(!screenshot_format("JSHOT%03d.PCX",14U,"JSHOT%03d.BMP"));

    // Exact x86 entry, pushes and hotkey CALL from the user's
    // SHA-verified jkgm(5).exe, not the upstream JK/OpenJKDF2 assumption.
    std::array<uint8_t,8> prologue{{
        0x81,0xEC,0xB8,0,0,0,0x53,0x55
    }};
    std::array<uint8_t,5> pcxpush{{0x68,0xA0,0x53,0x52,0x00}};
    std::array<uint8_t,5> bmppush{{0x68,0xB4,0x53,0x52,0x00}};
    std::array<uint8_t,5> hotkeycall{{0xE8,0x6D,0xFA,0xFF,0xFF}};
    std::array<char,14> pcx{},bmp{};
    std::memcpy(pcx.data(),"JSHOT%03d.PCX",14);
    std::memcpy(bmp.data(),"JSHOT%03d.BMP",14);

    auto read=[&](uintptr_t address,void *dst,size_t count) {
        auto copy=[&](auto const &src) {
            if(count>src.size())return false;
            std::memcpy(dst,src.data(),count);
            return true;
        };
        if(address==jk_game_screenshot_address)return copy(prologue);
        if(address==jk_screenshot_pcx_push)return copy(pcxpush);
        if(address==jk_screenshot_bmp_push)return copy(bmppush);
        if(address==jk_screenshot_hotkey_call)return copy(hotkeycall);
        if(address==jk_screenshot_pcx_format)return copy(pcx);
        if(address==jk_screenshot_bmp_format)return copy(bmp);
        return false;
    };
    assert(verified_jk_screenshot(read,jk_executable_base));
    assert(!verified_jk_screenshot(read,0x00500000U));

    // Each guard is necessary. Altering any byte must reject the detour.
    auto original_prologue=prologue;
    prologue[2]=0xA0;
    assert(!verified_jk_screenshot(read,jk_executable_base));
    prologue=original_prologue;

    auto original_push=bmppush;
    bmppush[1]=0xB5;
    assert(!verified_jk_screenshot(read,jk_executable_base));
    bmppush=original_push;

    auto original_name=bmp;
    bmp[0]='S';
    assert(!verified_jk_screenshot(read,jk_executable_base));
    bmp=original_name;

    auto original_call=hotkeycall;
    hotkeycall[1]=0x6E;
    assert(!verified_jk_screenshot(read,jk_executable_base));
    hotkeycall=original_call;
    assert(verified_jk_screenshot(read,jk_executable_base));

    // Flip glReadPixels' bottom-left origin into PNG's top-left origin.
    std::array<uint8_t,24> rgba{};
    for(unsigned i=0;i<12U;++i)rgba[i]=uint8_t(i+1U);
    for(unsigned i=12U;i<24U;++i)rgba[i]=uint8_t(i+101U);
    flip_rgba_rows(rgba.data(),3U,2U);
    for(unsigned i=0;i<12U;++i) {
        assert(rgba[i]==uint8_t(113U+i));
        assert(rgba[12U+i]==uint8_t(1U+i));
    }
    flip_rgba_rows(rgba.data(),3U,2U);
    for(unsigned i=0;i<12U;++i) {
        assert(rgba[i]==uint8_t(1U+i));
        assert(rgba[12U+i]==uint8_t(113U+i));
    }
    flip_rgba_rows(nullptr,0U,0U);

    // GL window readbacks can expose zero alpha for visible pixels.
    for(unsigned i=3U;i<rgba.size();i+=4U)rgba[i]=0U;
    force_opaque_alpha(rgba.data(),3U,2U);
    for(unsigned i=3U;i<rgba.size();i+=4U)assert(rgba[i]==255U);
    force_opaque_alpha(nullptr,0U,0U);
    return 0;
}
