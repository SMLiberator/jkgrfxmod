#pragma once
// Native JKGM saber swing state; verified by disassembly of jkgm(5).exe
// SHA256 afb4605c8259075c151ad6b034b98562be87ebe90369d4d9abaee09b396ed50f
// sithPuppet_DefaultCallback at 0x004E4F16 (marker 4):
//     or  dword ptr [eax+0x2a8], 0x2
// and 0x004E4F24 (marker 5):
//     and dword ptr [eax+0x2a8], 0xfffffffd
// The existing JKGM rdThing.pThing, SithThing.type and signature offsets
// are +0x44, +0x0c and +0x74 respectively.
#include <cstdint>

namespace jkgm {
inline constexpr uintptr_t jk_saber_rdthing_owner_offset=0x44U;
inline constexpr uintptr_t jk_saber_thing_type_offset=0x0cU;
inline constexpr uintptr_t jk_saber_thing_signature_offset=0x74U;
inline constexpr uintptr_t jk_saber_thing_flags_offset=0x2a8U;
inline constexpr uint32_t jk_saber_damage_flag=0x2U;

struct native_saber_damage_state {
    uint64_t owner=0;
    bool swing=false;
};

// Read is a guarded native memory reader: bool(uintptr_t,void*,size_t).
// Keep the owner-generation signature to prevent Thing slot recycling from
// bleeding the previous sabers' delayed poses into another Thing.
template<class Read>
bool read_native_saber_damage(Read const &read,uintptr_t rdthing,
                              native_saber_damage_state &result)
{
    result={};
    if(rdthing<0x10000U)return false;
    uint32_t owner=0U,kind=0U,signature=0U,flags=0U;
    if(!read(rdthing+jk_saber_rdthing_owner_offset,&owner,4U) ||
       owner<0x10000U ||
       !read(uintptr_t(owner)+jk_saber_thing_type_offset,&kind,4U) ||
       kind==0U || kind>15U ||
       !read(uintptr_t(owner)+jk_saber_thing_signature_offset,&signature,4U) ||
       !read(uintptr_t(owner)+jk_saber_thing_flags_offset,&flags,4U))
        return false;
    // A zero generation signature is possible for a valid newly
    // allocated Thing. The nonzero owner pointer still identifies it.
    result.owner=(uint64_t(owner)<<32)|signature;
    result.swing=(flags&jk_saber_damage_flag)!=0U;
    return true;
}
} // namespace jkgm
