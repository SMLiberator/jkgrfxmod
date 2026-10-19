#pragma once
// Pure JK stdPalEffects accumulator. Slots are updated by native COG verbs
// (including JK.dll extensions); no per-verb interception or palette writes.
// Matches the original 32-request array and filter/tint/add/fade ordering.
#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>

namespace jkgm {
    struct jk_palette_request {
        int32_t valid=0;
        int32_t id=0;
        int32_t filter[3]{};
        float tint[3]{};
        int32_t add[3]{};
        float fade=1.0f;
    };
    static_assert(sizeof(jk_palette_request)==0x30,
                  "Incorrect stdPalEffectRequest layout");

    struct jk_palette_effect {
        std::array<int,3> filter{{0,0,0}};
        std::array<float,3> tint{{0,0,0}};
        std::array<float,3> add{{0,0,0}};
        float fade=1.0f;
        bool active=false;
    };

    inline jk_palette_effect accumulate_jk_palette_effects(
        std::array<jk_palette_request,32> const &requests,
        uint32_t count, std::array<int32_t,4> enabled)
    {
        jk_palette_effect result;
        if(count>32U) return result;
        unsigned observed=0;
        for(auto const &req:requests) {
            if(!req.valid) continue;
            ++observed;
            for(int c=0;c<3;c++) {
                if(req.filter[c]) result.filter[c]=1;
                if(std::isfinite(req.tint[c]))
                    result.tint[c]+=req.tint[c];
                result.add[c]+=static_cast<float>(req.add[c]);
            }
            if(std::isfinite(req.fade))
                result.fade=std::min(result.fade,req.fade);
        }
        if(count!=observed) return {}; // read during mutation; try next frame
        for(int c=0;c<3;c++) {
            result.tint[c]=std::clamp(result.tint[c],-4.0f,4.0f);
            result.add[c]=std::clamp(result.add[c],-1024.0f,1024.0f);
        }
        result.fade=std::clamp(result.fade,0.0f,1.0f);
        if(!enabled[0]) result.filter={{0,0,0}};
        if(!enabled[1]) result.tint={{0,0,0}};
        if(!enabled[2]) result.add={{0,0,0}};
        if(!enabled[3]) result.fade=1.0f;
        bool const filter_active=result.filter[0]!=0 ||
                                 result.filter[1]!=0 ||
                                 result.filter[2]!=0;
        bool const tint_active=result.tint[0]!=0 ||
              result.tint[1]!=0 ||result.tint[2]!=0;
        bool const add_active=result.add[0]!=0 ||
             result.add[1]!=0 ||result.add[2]!=0;
        result.active=filter_active||tint_active||add_active||
                      result.fade<1.0f;
        return result;
    }
}
