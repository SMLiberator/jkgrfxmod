#pragma once
// Verified from the uploaded jkgm(5).exe, rdSprite_Draw (0x0046D359):
//   mov edx, [ecx+0x2c]      ; ecx = rdThing
//   mov [esi+0x30], edx      ; esi = rdProcEntry
// Do NOT confuse rdProcEntry+0x30 with rdThing+0x30. Native JK writes
// the explosion's animated cel to rdThing+0x2c; the GPU path reads it.
#include <cstddef>
#include <cstdint>

namespace jkgm {
constexpr uintptr_t native_sprite_wall_cel_offset=0x2cU;
template<class Reader>
inline bool read_native_sprite_wall_cel(
    Reader const &read,uintptr_t rdthing,int32_t &out)
{
    if(rdthing<0x10000U ||
       rdthing>0x7ff00000U-native_sprite_wall_cel_offset-sizeof(int32_t))
        return false;
    int32_t value=-1;
    if(!read(rdthing+native_sprite_wall_cel_offset,&value,sizeof(value)) ||
       value<-1 || value>255)return false;
    out=value;
    return true;
}
} // namespace jkgm
