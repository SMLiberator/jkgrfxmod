#pragma once
// Shadow-caster exclusions for verified SithThing flags and GeoModes.
// GPU-cached 3DOs additionally skip mod sunlight/specular, spotlights, and
// SSAO as receivers. Native-D3D 3DO rendering still needs per-Thing G-buffer
// tagging for receiver exclusion. Visibility and JK authored light remain.
#include <cstddef>
#include <cstdint>

namespace jkgm {
    constexpr uint32_t thingflag_skybox=0x20U;
    // Native SithThing exclusions: invisible, skybox/unlit and non-casting
    // decoration. These Things remain visible wherever JK normally draws
    // them; only their depth contribution to shadow maps is suppressed.
    constexpr uint32_t thingflag_shadow_exclusions=
        0x10U | thingflag_skybox | 0x80000U;
    inline bool thing_casts_sun_shadows(uint32_t thing_flags)
    {
        return (thing_flags&thingflag_shadow_exclusions)==0U;
    }
    // Visible native rendering is the fallback whenever GPU mesh lighting
    // exceeds the shader's light capacity. Its depth-only sidecar must still
    // be queued for STATIC geometry in that case, otherwise previously
    // shadow-casting 3DO architecture disappears from the sun map.
    // Noncasting 0x20 Things never become casters through this fallback.
    inline bool sun_shadow_native_sidecar_needed(
        bool gpu_visible_queued,bool camera_hud,bool model_cache,
        bool sun_shadows,bool dynamic_caster,bool light_overflow,
        uint64_t shadow_owner,uint32_t flags)
    {
        return !gpu_visible_queued && !camera_hud && model_cache &&
               sun_shadows && (dynamic_caster || light_overflow) &&
               shadow_owner!=0U && thing_casts_sun_shadows(flags);
    }

    inline bool thing_shadow_geomode_visible(int effective_mode,
                                              int mesh_mode)
    {
        return effective_mode>=3 && mesh_mode>=3;
    }

    // rdThing.pThing at +0x44 points to the containing SithThing.
    // SithThing embeds rdThing at +0xc4; its flags are the first DWORD.
    // Validate both directions and native Thing identity before using flags.
    // Unknown/invalid ownership MUST default to a normal sun caster.
    template<class Read>
    inline bool read_native_model_thing_flags(
        Read const &read,uintptr_t rd_thing,uint32_t &flags)
    {
        flags=0;
        if(rd_thing<0x10000U || rd_thing>0x7ffff000U)
            return false;
        uint32_t owner=0;
        if(!read(rd_thing+0x44U,&owner,sizeof(owner)) ||
           owner<0x10000U || owner>0x7ffff000U ||
           uintptr_t(owner)+0xc4U!=rd_thing)return false;
        uint32_t kind=0,identity=0,thing_flags=0;
        if(!read(uintptr_t(owner)+0x0cU,&kind,sizeof(kind)) ||
           kind==0 || kind>15 ||
           !read(uintptr_t(owner)+0x74U,&identity,sizeof(identity)) ||
           identity==0 ||
           !read(owner,&thing_flags,sizeof(thing_flags)))return false;
        flags=thing_flags;
        return true;
    }
}
