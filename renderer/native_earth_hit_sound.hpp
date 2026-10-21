#pragma once
// JKGM native sithCollision_HandleThingHitSurface: selecting the
// SoundClass mode after native hit magnitude, damage and metal tests.
// Patched-exe signature and exact instruction boundary verified in
// the supplied x86 jkgm.exe. The target is reached ONLY by the
// NON-METAL fallthrough of native surface-hit handling.
#include <array>
#include <cstdint>
namespace jkgm {
    constexpr uintptr_t native_earth_sound_branch=0x004E8EBAU;
    constexpr uintptr_t native_earth_sound_resume=0x004E8EC1U;
    constexpr std::array<uint8_t,8> native_earth_sound_bytes{{
        0x8b,0x4c,0x24,0x0c,0x51,0x6a,0x2e,0x56
    }};
    constexpr uint32_t native_surface_earth_flag=0x00080000U;
    constexpr int native_sound_hithard=46;
    constexpr int native_sound_hitmetal=47;
    constexpr int native_sound_hitearth=48;
    constexpr int native_sound_scrapehard=50;
    constexpr int native_sound_scrapemetal=51;
    constexpr int native_sound_scrapeearth=52;
    inline int native_nonmetal_surface_impact_mode(uint32_t flags) {
        return (flags&native_surface_earth_flag)
            ? native_sound_hitearth : native_sound_hithard;
    }
}
