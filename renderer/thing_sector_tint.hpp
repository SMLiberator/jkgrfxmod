#pragma once
// Per-Thing SithSector tint for GPU-cached rdModel3 instances.
// OpenJKDF2: SithThing.sector +0x5c, SithCamera.sector +0x18,
// SithSector.tint +0x10. Camera sector tint is also present in
// the native stdPalEffects aggregate; replace only that contribution.
// No native palette state or engine memory is modified.
#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>

namespace jkgm {
    inline bool jk_tint_native_ptr(uint32_t value) {
        return value>=0x10000U && value<0x7ff00000U;
    }

    template <class Read>
    bool read_jk_sector_tint(Read const &read,uint32_t sector,
                             std::array<float,3> &out) {
        if(!jk_tint_native_ptr(sector))return false;
        std::array<float,3> tint{};
        if(!read(uintptr_t(sector)+0x10U,tint.data(),sizeof(tint)))
            return false;
        for(float &v:tint) {
            if(!std::isfinite(v))return false;
            v=std::clamp(v,0.0f,1.0f);
        }
        out=tint;
        return true;
    }

    template <class Read>
    bool read_jk_thing_sector_tint(Read const &read,uintptr_t thing,
                                   std::array<float,3> &out) {
        if(thing<0x10000U || thing>=0x7ff00000U)return false;
        uint32_t sector=0;
        if(!read(thing+0x5cU,&sector,sizeof(sector)))return false;
        return read_jk_sector_tint(read,sector,out);
    }

    template <class Read>
    bool read_jk_camera_sector_tint(Read const &read,
                                    std::array<float,3> &out) {
        // The OpenJKDF2 global SithCamera pointer (0x82f104) is NOT
        // available at that address in jkgm.exe. Use this build's verified
        // SithCamera array/index, also used by read_engine_camera().
        // Only seven normal camera slots are supported; invalid/temporary
        // camera selections retain vanilla tint instead of guessing.
        constexpr uintptr_t camera_array=0x008EC2C0U;
        constexpr uintptr_t camera_index=0x008EE4E8U;
        constexpr uintptr_t camera_stride=0x4E0U;
        constexpr int32_t camera_count=7;
        int32_t index=-1;
        if(!read(camera_index,&index,sizeof(index)) ||
           index<0 || index>=camera_count)return false;
        uintptr_t const camera=camera_array+
            uintptr_t(index)*camera_stride;
        uint32_t sector=0;
        if(!read(camera+0x18U,&sector,sizeof(sector)))
            return false;
        return read_jk_sector_tint(read,sector,out);
    }

    inline std::array<float,3> jk_thing_tint_delta(
        std::array<float,3> const &thing_tint,
        std::array<float,3> const &camera_tint) {
        return {{thing_tint[0]-camera_tint[0],
                 thing_tint[1]-camera_tint[1],
                 thing_tint[2]-camera_tint[2]}};
    }
}
