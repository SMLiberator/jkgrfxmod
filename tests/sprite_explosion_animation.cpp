#include "renderer/sprite_explosion_animation.hpp"
#include <array>
#include <cassert>
#include <cstdint>
#include <cstring>

int main()
{
    constexpr uintptr_t base=0x120000U,owner=base+0x100U;
    constexpr uintptr_t rd=owner+0xc4U;
    std::array<uint8_t,0x1000U> memory{};
    const auto put=[&](uintptr_t ptr,uint32_t value) {
        assert(ptr>=base && ptr-base+4U<=memory.size());
        std::memcpy(memory.data()+(ptr-base),&value,4U);
    };
    auto read=[&](uintptr_t ptr,void *out,size_t len)->bool {
        if(ptr<base || ptr-base>memory.size() ||
           len>memory.size()-(ptr-base))return false;
        std::memcpy(out,memory.data()+(ptr-base),len);
        return true;
    };
    put(rd+0x44U,static_cast<uint32_t>(owner));
    put(owner+0x0cU,6U); // SithThing type: explosion
    put(rd,4U);           // rdThing type: SPRITE3
    put(owner+0x130U,1U);// explosionParams.flags: animated SPR
    assert(jkgm::native_animated_explosion_sprite(read,rd));

    put(owner+0x130U,0U);
    assert(!jkgm::native_animated_explosion_sprite(read,rd));
    put(owner+0x130U,2U);
    assert(!jkgm::native_animated_explosion_sprite(read,rd));
    put(owner+0x130U,3U); // animation and unrelated explosion flag
    assert(jkgm::native_animated_explosion_sprite(read,rd));
    put(owner+0x0cU,2U); // actor using same SPR: GPU eligible
    assert(!jkgm::native_animated_explosion_sprite(read,rd));
    put(owner+0x0cU,6U);
    put(rd,3U); // not RD_THING_SPRITE3
    assert(!jkgm::native_animated_explosion_sprite(read,rd));
    put(rd,4U);
    put(rd+0x44U,0U); // no trusted owning SithThing
    assert(!jkgm::native_animated_explosion_sprite(read,rd));
    put(rd+0x44U,static_cast<uint32_t>(owner));
    assert(!jkgm::native_animated_explosion_sprite(read,rd+4U));
    assert(!jkgm::native_animated_explosion_sprite(read,0U));
    auto unreadable=[&](uintptr_t,void*,size_t)->bool {return false;};
    assert(!jkgm::native_animated_explosion_sprite(unreadable,rd));

    // The explosion's 'timer' is stored as initial msecLifeLeft on
    // sithExplosion_CreateThing; JK computes integer ms per cel from
    // that value and the material's num_texinfo. No renderer clock exists.
    constexpr uintptr_t mat=base+0x600U;
    put(owner+0x130U,1U); // animate sprite
    put(mat+0x64U,4U); // 4 MAT cels
    put(owner+0x134U,1000U); // initial 1000 ms
    int32_t selected=-1;
    auto at=[&](int32_t remaining,int32_t native_cel,int32_t expected) {
        put(owner+0x18U,static_cast<uint32_t>(remaining));
        assert(jkgm::native_explosion_sprite_cel(
            read,rd,mat,native_cel,selected));
        assert(selected==expected);
    };
    at(1000,0,0); // first frame
    at(751,0,0);
    at(750,0,1); // 250 ms per cel
    at(500,0,2);
    at(250,0,3);
    at(0,0,3); // final frame clamps
    at(750,2,2); // real JK cel takes priority if it advanced
    at(600,1,1); // no substitute if native already advanced

    put(mat+0x64U,8U);
    at(875,0,1);
    at(125,0,7);
    put(mat+0x64U,4U);
    put(owner+0x134U,101U); // native interval truncates to 25 ms
    at(76,0,1);
    at(26,0,3);
    put(owner+0x134U,1000U);
    put(owner+0x18U,0U);
    assert(!jkgm::native_explosion_sprite_cel(
        read,rd,mat,4,selected)); // native out-of-bounds cel
    put(mat+0x64U,1U);
    assert(!jkgm::native_explosion_sprite_cel(
        read,rd,mat,0,selected));
    put(mat+0x64U,4U);
    put(owner+0x134U,0U);
    assert(!jkgm::native_explosion_sprite_cel(
        read,rd,mat,0,selected));
    put(owner+0x134U,1000U);
    put(owner+0x18U,1001U);
    assert(!jkgm::native_explosion_sprite_cel(
        read,rd,mat,0,selected));
    put(owner+0x18U,static_cast<uint32_t>(-1));
    assert(!jkgm::native_explosion_sprite_cel(
        read,rd,mat,0,selected));
    put(owner+0x18U,500U);
    put(owner+0x130U,0U);
    assert(!jkgm::native_explosion_sprite_cel(
        read,rd,mat,0,selected)); // non-animated explosions unaffected
    put(owner+0x130U,1U);
    assert(!jkgm::native_explosion_sprite_cel(
        unreadable,rd,mat,0,selected));
}
