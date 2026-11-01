#pragma once
// JK's explosion typeflags bit 0 requests a native sprite cel animation.
// JK's own cel is authoritative when advanced. When it remains zero,
// reconstruct the animation from the explosion's ORIGINAL lifetime and
// current remaining lifetime, following the native interval truncation.
// Verified layout: SithThing.renderData at +0xc4, rdThing.pThing at +0x44,
// SithThing.type at +0x0c and explosionParams.flags at +0x130.
// Do not infer explosion flags from general SithThing.flags at +0x00.
#include <cstddef>
#include <cstdint>

namespace jkgm {
template<class Read>
inline bool native_animated_explosion_sprite(Read const &read,uintptr_t rdthing)
{
    if(rdthing<0x10000U || rdthing>0x7ff00000U)return false;
    uint32_t owner=0U,type=0U,render_type=0U,explosion_flags=0U;
    if(!read(rdthing+0x44U,&owner,sizeof(owner)) ||
       owner<0x10000U || owner>0x7ff00000U-0x134U ||
       uintptr_t(owner)+0xc4U!=rdthing ||
       !read(rdthing,&render_type,sizeof(render_type)) ||
       !read(uintptr_t(owner)+0x0cU,&type,sizeof(type)) ||
       !read(uintptr_t(owner)+0x130U,&explosion_flags,
             sizeof(explosion_flags)))return false;
    return type==6U && render_type==4U && (explosion_flags&0x1U)!=0U;
}
/*
 * OpenJKDF2 sithExplosion_CreateThing starts a native sithSurface animation
 * at fps = num_texinfo / (initial_lifetime_ms / 1000).
 * sithSurface_sub_4F00A0 truncates the interval to integer milliseconds:
 *     period_ms = uint32_t(initial_lifetime_ms / num_texinfo).
 * The tick then advances rdThing.wallCel. Prefer that native cel whenever
 * it has advanced; if it remains 0, recover the frame from the existing
 * SithThing lifetime fields, never from a new renderer clock.
 *
 * SithThing +0x18: msecLifeLeft (from the template 'timer' field).
 * SithThing +0x134: explosionParams.msecLifeLeft (initial lifetime).
 * rdMaterial +0x64: num_texinfo. Cel count is limited to 16.
 */
template<class Read>
inline bool native_explosion_sprite_cel(
    Read const &read,uintptr_t rdthing,uintptr_t material,
    int32_t native_cel,int32_t &out)
{
    if(!native_animated_explosion_sprite(read,rdthing) ||
       material<0x10000U || material>0x7ff00000U-0xb4U)
        return false;
    uint32_t owner=0U,cels=0U,initial_ms=0U;
    int32_t remaining_ms=0;
    if(!read(rdthing+0x44U,&owner,sizeof(owner)) ||
       !read(material+0x64U,&cels,sizeof(cels)) ||
       !read(uintptr_t(owner)+0x18U,&remaining_ms,sizeof(remaining_ms)) ||
       !read(uintptr_t(owner)+0x134U,&initial_ms,sizeof(initial_ms)) ||
       cels<2U || cels>16U || initial_ms<cels ||
       initial_ms>0x7fffffffU || remaining_ms<0 ||
       uint32_t(remaining_ms)>initial_ms ||
       native_cel<0 || uint32_t(native_cel)>=cels)
        return false;
    uint32_t const period_ms=initial_ms/cels;
    if(period_ms==0U)return false;
    uint32_t const elapsed_ms=initial_ms-uint32_t(remaining_ms);
    uint32_t const estimated=elapsed_ms/period_ms;
    // Preserve JK's authoritative nonzero cel whenever it advances,
    // including when the tick ran at a different point in this frame.
    out=native_cel>0 ?native_cel
        :static_cast<int32_t>(estimated>=cels?cels-1U:estimated);
    return true;
}

} // namespace jkgm
