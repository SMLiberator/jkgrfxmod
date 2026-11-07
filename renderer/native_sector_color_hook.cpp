#include "native_sector_color_hook.hpp"

#include "base/log.hpp"
#include "native_jk_palette_effects.hpp"
#include <Windows.h>
#include <atomic>
#include <cstdint>
#include <cstring>
#include <detours/detours.h>

namespace {
// This build's rdSetColorEffects has been independently signature-verified
// by read_native_jk_hardware_color_effects(). Detour the *copy* of the
// combined native palette effects, not its 32 original COG requests.
// Native geometry then receives all other global effects unchanged.
using native_color_effect_fn=int (__cdecl*)(jkgm::jk_hardware_color_effect *);
native_color_effect_fn RealNativeColorEffects=
    reinterpret_cast<native_color_effect_fn>(0x0043AE60U);

int __cdecl HookedNativeColorEffects(jkgm::jk_hardware_color_effect *incoming)
{
    if(!incoming || !jkgm::jk_palette_memory_readable(
           incoming,sizeof(*incoming)))
        return RealNativeColorEffects(incoming);
    auto const read=[](uintptr_t addr,void *dst,size_t len)->bool {
        if(!jkgm::jk_palette_memory_readable(
               reinterpret_cast<void const *>(addr),len))return false;
        std::memcpy(dst,reinterpret_cast<void const *>(addr),len);
        return true;
    };
    auto const probe=jkgm::probe_native_sector_palette(
        read,reinterpret_cast<uintptr_t>(incoming));
    jkgm::native_sector_palette_mode().store(
        !probe.layout_valid?0:(probe.sector_request_active?2:1),
        std::memory_order_relaxed);
    static int last_mode=-1;
    int const mode=jkgm::native_sector_palette_mode().load(
        std::memory_order_relaxed);
    if(mode!=last_mode) {
        last_mode=mode;
        LOG_INFO("SECTOR PALETTE bridge mode=",mode,
                 " (0=unverified 1=freed 2=active)",
                 " sector_slot=",probe.sector_slot);
    }
    if(!probe.layout_valid || !probe.sector_request_active)
        return RealNativeColorEffects(incoming);
    jkgm::jk_hardware_color_effect neutral=*incoming;
    for(int ch=0;ch<3;++ch)
        neutral.tint[ch]-=probe.sector_tint[ch];
    // The native function copies all 40 bytes. It does NOT retain pointer.
    // All other requests' tint, RGB filters/adds and fade stay intact.
    return RealNativeColorEffects(&neutral);
}
}

namespace jkgm::native_sector_color_hook {
bool request(bool gpu_cache_enabled)
{
    if(!gpu_cache_enabled)return false;
    constexpr uint8_t signature[]{
        0x56,0x8b,0x74,0x24,0x08,0x57,0xb9,0x0a,
        0x00,0x00,0x00,0xbf,0x00,0x9e,0x88,0x00
    };
    void const *fn=reinterpret_cast<void const *>(0x0043AE60U);
    if(reinterpret_cast<uintptr_t>(GetModuleHandleA(nullptr))!=0x00400000U ||
       !jkgm::jk_palette_memory_readable(fn,sizeof(signature)) ||
       std::memcmp(fn,signature,sizeof(signature))!=0) {
        LOG_WARNING("SECTOR PALETTE hook skipped: unknown native rdSetColorEffects");
        return false;
    }
    LONG const err=DetourAttach(&(PVOID&)RealNativeColorEffects,
                                HookedNativeColorEffects);
    if(err!=NO_ERROR) {
        LOG_WARNING("SECTOR PALETTE native hook failed: ",err);
        return false;
    }
    return true;
}

void detach()
{
    DetourDetach(&(PVOID&)RealNativeColorEffects,HookedNativeColorEffects);
}
}
