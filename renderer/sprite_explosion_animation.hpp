#pragma once
// JK's explosion typeflags bit 0 requests a native sprite cel animation.
// This read-only predicate guards an interim native rendering fallback.
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
} // namespace jkgm
