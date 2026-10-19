#pragma once
// Fixed locations verified in the user's JK23-based jkgm.exe:
// 0x4290f0 stdPalEffects_GatherEffects,
// 0x55be90 stdPalEffects_numEffectRequests,
// 0x866140 stdPalEffects_state,
// 0x8661a0 stdPalEffects_aEffects[32].
// JK.dll changes the same native requests; no DLL patch is needed for color.
#include "jk_palette_effects.hpp"
#include <Windows.h>
#include <array>
#include <cstdint>
#include <cstring>

namespace jkgm {
    inline bool jk_palette_memory_readable(void const *p,size_t length)
    {
        if(!p || !length) return false;
        MEMORY_BASIC_INFORMATION info{};
        if(!VirtualQuery(p,&info,sizeof(info)) ||
           info.State!=MEM_COMMIT ||
           (info.Protect&(PAGE_GUARD|PAGE_NOACCESS))) return false;
        auto const addr=reinterpret_cast<uintptr_t>(p);
        auto const end=reinterpret_cast<uintptr_t>(info.BaseAddress)+info.RegionSize;
        return addr+length>=addr && addr+length<=end;
    }
    inline jk_palette_effect read_native_jk_palette_effects()
    {
        static bool const supported=[] {
            constexpr uint8_t signature[]{
                0x83,0xec,0x28,0x53,0x55,0x8b,0x2d,0x90,0xbe,0x55,0x00
            };
            void const *code=reinterpret_cast<void const *>(0x004290f0U);
            return reinterpret_cast<uintptr_t>(GetModuleHandleA(nullptr))==
                       0x00400000U &&
                   jk_palette_memory_readable(code,sizeof(signature)) &&
                   std::memcmp(code,signature,sizeof(signature))==0;
        }();
        if(!supported) return {};
        auto const *count_ptr=reinterpret_cast<void const *>(0x0055be90U);
        auto const *state_ptr=reinterpret_cast<void const *>(0x00866144U);
        auto const *slots_ptr=reinterpret_cast<void const *>(0x008661a0U);
        if(!jk_palette_memory_readable(count_ptr,sizeof(uint32_t)) ||
           !jk_palette_memory_readable(state_ptr,4*sizeof(int32_t)) ||
           !jk_palette_memory_readable(slots_ptr,32*sizeof(jk_palette_request)))
            return {};
        uint32_t count=0;
        std::array<int32_t,4> enabled{};
        std::array<jk_palette_request,32> slots{};
        std::memcpy(&count,count_ptr,sizeof(count));
        if(count==0 || count>32U) return {};
        std::memcpy(enabled.data(),state_ptr,sizeof(enabled));
        std::memcpy(slots.data(),slots_ptr,sizeof(slots));
        return accumulate_jk_palette_effects(slots,count,enabled);
    }
}
