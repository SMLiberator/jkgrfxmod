#include "renderer/native_screenshot.hpp"
#include <array>
#include <cassert>
#include <cstdint>
#include <cstring>
#include <string>
int main()
{
    using namespace jkgm::native_screenshot;
    assert(png_filename(0U)=="SHOT0000.PNG");
    assert(png_filename(12U)=="SHOT0012.PNG");
    assert(png_filename(9999U)=="SHOT9999.PNG");
    assert(screenshot_format("SHOT%04d.BMP",16U));
    assert(screenshot_format("shot%04d.bmp",16U));
    assert(!screenshot_format("SHOT%04d.PNG",16U));
    assert(!screenshot_format("FAKE%04d.BMP",16U));

    std::array<uint8_t,signature_code_bytes> code{};
    code[0]=0x55; // valid x86 prologue
    constexpr uint32_t format_address=0x00409000U;
    std::memcpy(code.data()+12U,&format_address,sizeof(format_address));
    char const format[16]="SHOT%04d.BMP";
    auto read=[&](uintptr_t address,void *dst,size_t count) {
        if(address==jk_game_screenshot_address && count<=code.size()) {
            std::memcpy(dst,code.data(),count);
            return true;
        }
        if(address==format_address && count<=sizeof(format)) {
            std::memcpy(dst,format,count);
            return true;
        }
        return false;
    };
    assert(verified_jk_screenshot(read,jk_executable_base));
    assert(!verified_jk_screenshot(read,0x00500000U));
    auto no_xref=code;
    std::memset(code.data()+12U,0,4U);
    assert(!verified_jk_screenshot(read,jk_executable_base));
    code=no_xref;
    code[0]=0xCC;
    assert(!verified_jk_screenshot(read,jk_executable_base));
    code=no_xref;

    // OpenGL readback is bottom-up and PNG encoder expects top-down.
    // 3x2 RGBA rows also exercise strides that are not a multiple of 8.
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
    // Default OpenGL backbuffers need not have an alpha plane. In that
    // case glReadPixels(GL_RGBA) may return zero alpha for visible pixels.
    // Saved PNGs must always be fully opaque, never invisible/magenta.
    for(unsigned i=3U;i<rgba.size();i+=4U)rgba[i]=0U;
    force_opaque_alpha(rgba.data(),3U,2U);
    for(unsigned i=3U;i<rgba.size();i+=4U)assert(rgba[i]==255U);
    force_opaque_alpha(nullptr,0U,0U);
    return 0;
}
