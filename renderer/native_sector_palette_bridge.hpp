#pragma once
// Native stdPalEffects has 32 requests. Two player-owned entries normally
// occupy slots 0 (dynamic tint/add, request id 1) and 1 (sector tint, id 2).
// Never assume that an arbitrary combined RGB tint is entirely sector tint.
// JK's rdSetColorEffects receives &stdPalEffects_state.effect. In OpenJKDF2,
// stdPalEffects_state.effect is at +0x14 and the request array at +0x60;
// derive the array from that *actual argument*, not from exe symbol addresses.
#include "jk_palette_effects.hpp"
#include <array>
#include <atomic>
#include <cmath>
#include <cstdint>
#include <cstddef>

namespace jkgm {
    struct native_sector_palette_probe {
        bool layout_valid=false;
        bool sector_request_active=false;
        uint32_t sector_slot=32U;
        std::array<float,3> sector_tint{};
    };

    // 0: bridge has not verified a native request pool.
    // 1: verified pool; sector request is free/disabled.
    // 2: verified pool; sector request exists and can be localized.
    inline std::atomic<int> &native_sector_palette_mode() {
        static std::atomic<int> value{0};
        return value;
    }
    // Detours replace the first bytes of rdSetColorEffects with a jump.
    // The existing native hardware effect reader must recognize our
    // verified installed detour instead of expecting the original prologue.
    inline std::atomic<bool> &native_sector_palette_hook_installed() {
        static std::atomic<bool> value{false};
        return value;
    }

    template<class Read>
    native_sector_palette_probe probe_native_sector_palette(
        Read const &read,uintptr_t combined_effect_ptr)
    {
        native_sector_palette_probe result{};
        if(combined_effect_ptr<0x10000U ||
           combined_effect_ptr>0x7ffff000U)return result;
        constexpr uintptr_t state_effect_offset=0x14U;
        constexpr uintptr_t request_pool_offset=0x60U;
        uintptr_t const pool=combined_effect_ptr-state_effect_offset+
                              request_pool_offset;
        std::array<jk_palette_request,32> req{};
        if(!read(pool,req.data(),sizeof(req)))return result;
        // JK allocates player ids 1 and 2, but the request *slot*
        // depends on what was previously freed/reallocated. Validate the
        // native array itself, then locate id 2 by metadata, not by index.
        // FreeRequest leaves id 2 behind with isValid=0 until reused.
        bool player_slot_seen=false;
        for(auto const &entry:req) {
            if(entry.valid!=0 && entry.valid!=1)return result;
            if(entry.id==1 || entry.id==2)player_slot_seen=true;
        }
        if(!player_slot_seen)return result;
        result.layout_valid=true;
        for(size_t i=0;i<req.size();++i) {
            auto const &entry=req[i];
            if(entry.valid!=1 || entry.id!=2)continue;
            bool good=std::isfinite(entry.fade) &&
                      entry.fade>=0.0f && entry.fade<=1.0f;
            for(float tint:entry.tint)
                good=good && std::isfinite(tint) &&
                    tint>=-4.0f && tint<=4.0f;
            if(!good) {
                result.layout_valid=false;
                return result;
            }
            result.sector_request_active=true;
            result.sector_slot=static_cast<uint32_t>(i);
            for(int c=0;c<3;++c)result.sector_tint[c]=entry.tint[c];
            break;
        }
        return result;
    }

    inline jk_palette_effect without_native_sector_tint(
        jk_palette_effect const &combined,
        native_sector_palette_probe const &probe) {
        if(!probe.layout_valid || !probe.sector_request_active)
            return combined;
        auto result=combined;
        for(int c=0;c<3;c++)
            result.tint[c]-=probe.sector_tint[c];
        // Do not alter filter/add/fade even if the sector request carries
        // unusual values. Those effects must remain global.
        result.active=false;
        for(int c=0;c<3;c++)
            result.active=result.active||result.filter[c]!=0 ||
                          result.tint[c]!=0.0f ||
                          result.add[c]!=0.0f;
        result.active=result.active||result.fade<1.0f;
        return result;
    }
}
