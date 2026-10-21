#pragma once
// Exact JKGM (patched 32-bit EXE SHA256 afb4605...) attached-Thing friction
// patch. Used for face attachments ONLY, not ordinary Thing attachments.
// sithPhysics_UpdateAttachedThingPhysics = 0x004F76C0.
// At 0x004F7833 a valid instruction boundary joins both world and Thing
// face friction paths, with ESI = the moving/attached Thing; [ESP+0x10]
// holds the just-selected native surface-friction multiplier.
// The displaced instructions are MOV ECX,[ESI+0x1D0]; MOV EDX,ECX.
// The caller must validate the exact original bytes before detouring.
#include <array>
#include <cstdint>
namespace jkgm {
    constexpr uintptr_t native_thingface_ice_point=0x004F7833U;
    constexpr std::array<uint8_t,8> native_thingface_ice_bytes{{
        0x8b,0x8e,0xd0,0x01,0x00,0x00,0x8b,0xd1
    }};
    // These have no discovered reads in reference OpenJKDF2 code.
    // They still require verification against customized third-party EXEs.
    constexpr uint32_t thingflag_icy=0x00010000U;
    constexpr uint32_t thingflag_very_icy=0x40000000U;
    constexpr uint32_t attachment_to_world_face=0x1U;
    constexpr uint32_t attachment_to_thing_face=0x2U;
    inline float native_thingface_ice_multiplier(
        uint32_t attach_flags,uint32_t attached_thing_flags) {
        if(!(attach_flags&attachment_to_thing_face))return 1.0f;
        if(attached_thing_flags&thingflag_very_icy)return 0.1f;
        if(attached_thing_flags&thingflag_icy)return 0.3f;
        return 1.0f;
    }
}
