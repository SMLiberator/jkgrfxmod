#include "renderer/native_spawn_parent.hpp"

#include <array>
#include <cassert>
#include <cstdint>

struct Thing { Thing *parent=nullptr; };

int main() {
    using namespace jkgm::native_spawn_parent;
    Thing player{}, projectile{&player}, unowned{}, explosion{&player};
    // creatething preserves the original parent's identity.
    assert(choose_parent(&player,&projectile)==&player);
    // Without an original parent, the newly created source is the parent.
    assert(choose_parent(static_cast<Thing*>(nullptr),&unowned)==&unowned);
    // Projectile explosions and fleshhit already use resolved root owner.
    auto root=[](Thing *t) { while(t->parent)t=t->parent;return t; };
    assert(root_or_source(&projectile,root)==&player);
    assert(root_or_source(&unowned,root)==&unowned);
    assert(root_or_source(&explosion,root)==&player);
    assert(root_or_source(static_cast<Thing*>(nullptr),root)==nullptr);

    // Verify actual native CALL bytes against jkgm(5).exe at each changed site.
    constexpr std::array<std::uintptr_t,5> addresses{{
        0x004cdf7fU,0x004fbe46U,0x004ed3b6U,
        0x004d3b47U,0x004d41cfU
    }};
    constexpr std::array<std::array<std::uint8_t,5>,5> known{{
        {{0xe8,0x2c,0xfd,0xff,0xff}},
        {{0xe8,0x65,0x1e,0xfd,0xff}},
        {{0xe8,0xf5,0x08,0xfe,0xff}},
        {{0xe8,0x64,0xa1,0xff,0xff}},
        {{0xe8,0xdc,0x9a,0xff,0xff}}
    }};
    for(std::size_t i=0;i<addresses.size();++i) {
        std::array<std::uint8_t,5> actual{};
        assert(make_call(addresses[i],0x004cdcb0U,actual));
        assert(actual==known[i]);
    }
    std::array<std::uint8_t,5> unused{};
    assert(!make_call(0U,0x90000000U,unused));
}
