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
}
