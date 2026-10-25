#pragma once
// JKGM-only skybox convention used by COG levels: SithThing.flags bit 0x20.
// This affects DIRECTIONAL SUN SHADOW CASTING only, not visibility, lighting,
// collisions, normal/material state, or the independent spotlight shadows.
#include <cstddef>
#include <cstdint>

namespace jkgm {
    constexpr uint32_t thingflag_skybox=0x20U;
    inline bool thing_casts_sun_shadows(uint32_t thing_flags)
    {
        return (thing_flags&thingflag_skybox)==0U;
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
        if(!read || rd_thing<0x10000U || rd_thing>0x7ffff000U)
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
