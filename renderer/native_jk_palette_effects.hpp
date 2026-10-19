#pragma once
// Hardware JK23 copies the combined stdPalEffect at rdSetColorEffects
// (0x0043AE60) to rdroid_curColorEffects (0x00889E00), verified in the
// supplied exe. Native D3D vertices already incorporate these effects:
// only bypassed GPU 3DO lighting requires additional processing.
#include "jk_palette_effects.hpp"
#include <Windows.h>
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstring>

namespace jkgm {
    inline bool jk_palette_memory_readable(void const *p,size_t size) {
        if(!p || !size) return false;
        MEMORY_BASIC_INFORMATION mbi{};
        if(!VirtualQuery(p,&mbi,sizeof(mbi)) || mbi.State!=MEM_COMMIT ||
           (mbi.Protect&(PAGE_NOACCESS|PAGE_GUARD))) return false;
        auto const addr=reinterpret_cast<uintptr_t>(p);
        auto const end=reinterpret_cast<uintptr_t>(mbi.BaseAddress)+mbi.RegionSize;
        return addr+size>=addr && addr+size<=end;
    }
    struct jk_hardware_color_effect {
        int32_t filter[3];
        float tint[3];
        int32_t add[3];
        float fade;
    };
    static_assert(sizeof(jk_hardware_color_effect)==40,"Incorrect rdroid effect layout");
    inline jk_palette_effect read_native_jk_hardware_color_effects() {
        static bool const supported=[] {
            constexpr uint8_t sig[]{
                0x56,0x8b,0x74,0x24,0x08,0x57,0xb9,0x0a,
                0x00,0x00,0x00,0xbf,0x00,0x9e,0x88,0x00
            };
            void const *code=reinterpret_cast<void const *>(0x0043AE60U);
            return reinterpret_cast<uintptr_t>(GetModuleHandleA(nullptr))==
                       0x00400000U &&
                   jk_palette_memory_readable(code,sizeof(sig)) &&
                   std::memcmp(code,sig,sizeof(sig))==0;
        }();
        if(!supported) return {};
        void const *p=reinterpret_cast<void const *>(0x00889E00U);
        if(!jk_palette_memory_readable(p,sizeof(jk_hardware_color_effect)))
            return {};
        jk_hardware_color_effect raw{};
        std::memcpy(&raw,p,sizeof(raw));
        jk_palette_effect result;
        for(int i=0;i<3;i++) {
            if(!std::isfinite(raw.tint[i])) return {};
            result.filter[i]=raw.filter[i]!=0?1:0;
            result.tint[i]=std::clamp(raw.tint[i],-4.0f,4.0f);
            result.add[i]=std::clamp(float(raw.add[i]),-1024.0f,1024.0f);
            result.active=result.active||result.filter[i]!=0 ||
                          result.tint[i]!=0||result.add[i]!=0;
        }
        if(!std::isfinite(raw.fade)) return {};
        result.fade=std::clamp(raw.fade,0.0f,1.0f);
        result.active=result.active || result.fade<1.0f;
        return result;
    }
}
