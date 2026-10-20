#include "renderer/sprite_alpha_tag.hpp"
#include <cassert>
#include <cmath>
#include <cstdint>
#include <limits>
using namespace jkgm;
int main() {
    uint32_t tag=0;
    assert(sprite_alpha_encode(0.5f,false,tag));
    assert((tag&sprite_alpha_tag_mask)==sprite_alpha_tag_magic);
    // Originally opaque faces should use 255*opacity despite being
    // temporarily marked as translucent for native GPU batch routing.
    assert(sprite_alpha_override(tag,90)==128);
    assert(sprite_alpha_override(tag,255)==128);
    assert(sprite_alpha_encode(0.25f,true,tag));
    assert(sprite_alpha_override(tag,90)==23); // Preserve existing translucency.
    assert(sprite_alpha_encode(1.0f,false,tag));
    assert(sprite_alpha_override(tag,90)==255);
    assert(sprite_alpha_encode(0.0f,false,tag));
    assert(sprite_alpha_override(tag,90)==0);
    assert(!sprite_alpha_encode(-0.1f,false,tag));
    assert(!sprite_alpha_encode(1.1f,false,tag));
    assert(!sprite_alpha_encode(std::numeric_limits<float>::quiet_NaN(),false,tag));
    assert(sprite_alpha_override(0x00000000U,90)==-1);
    assert(sprite_alpha_override(0x51A80040U,90)==-1);
    assert(sprite_alpha_override(sprite_alpha_tag_magic|0x40U,-1)==-1);
}
