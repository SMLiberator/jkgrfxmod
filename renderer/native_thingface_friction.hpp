#pragma once
// Patched 32-bit JKGM: collision-face ice modifier.
// This intercepts the native surface-friction coefficient only for Things
// attached to a supporting Thing face (attachment flag 0x2).
// 0x004F7833 is the verified join in sithPhysics_UpdateAttachedThingPhysics.
// At this point [ESP+0x10] is the coefficient; ESI is the attached Thing.
// The trampoline replays MOV ECX,[ESI+0x1D0]; MOV EDX,ECX.
// This is a guarded, JKGM-specific extension; other executables need
// independent verification. It does not edit surfDrag/staticDrag fields.
#include <array>
#include <cstdint>
namespace jkgm {
    constexpr uintptr_t native_thingface_ice_point=0x004F7833U;
    constexpr std::array<uint8_t,8> native_thingface_ice_bytes{{
        0x8b,0x8e,0xd0,0x01,0x00,0x00,0x8b,0xd1
    }};
    // User tested this bit in-game: no native render suppression in JKGM.
    // It remains an experimental custom reservation, not universal JK behavior.
    constexpr uint32_t thingflag_icy=0x80000000U;
    constexpr uint32_t attachment_to_thing_face=0x2U;
    constexpr float icy_friction_coefficient=0.3f;
    inline float native_thingface_ice_multiplier(
        uint32_t attach_flags,uint32_t attached_thing_flags) {
        return (attach_flags&attachment_to_thing_face) &&
               (attached_thing_flags&thingflag_icy)
            ? icy_friction_coefficient : 1.0f;
    }
}
