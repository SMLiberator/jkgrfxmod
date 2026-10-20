#pragma once
// JK23 rdProcEntry.extraData is zero for SPR faces (rdSprite_Draw passes
// AddProcFace(0,...)). Encode an instance alpha override ONLY on sprite
// faces with polygon geometry, never on unrelated world/3DO primitives.
// The cache converter reads this at the face-alpha selection site.
#include <algorithm>
#include <cmath>
#include <cstdint>

namespace jkgm {
    constexpr uint32_t sprite_alpha_tag_magic=0x51A70000U;
    constexpr uint32_t sprite_alpha_tag_mask=0xfffffc00U;
    constexpr uint32_t sprite_alpha_flare=0x200U;
    constexpr uint32_t sprite_alpha_original_translucent=0x100U;

    inline bool sprite_alpha_encode(float factor,bool originally_translucent,
                                    uint32_t &tag,bool flare=false)
    {
        if(!std::isfinite(factor)||factor<0.0f||factor>1.0f)
            return false;
        unsigned const q=static_cast<unsigned>(std::lround(factor*255.0f));
        tag=sprite_alpha_tag_magic |
            (originally_translucent?sprite_alpha_original_translucent:0U) |
            (flare?sprite_alpha_flare:0U) | q;
        return true;
    }

    inline int sprite_alpha_override(uint32_t tag,int native_alpha)
    {
        if((tag&sprite_alpha_tag_mask)!=sprite_alpha_tag_magic)
            return -1;
        if(native_alpha<0||native_alpha>255) return -1;
        int const src=(tag&sprite_alpha_original_translucent)?
            native_alpha:255;
        return (src*static_cast<int>(tag&0xffU)+127)/255;
    }
}
