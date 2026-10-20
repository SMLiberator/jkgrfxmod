#include "renderer/sprite_type_registry.hpp"
#include "renderer/sprite_orientation.hpp"
#include <array>
#include <cassert>
#include <cstdint>
#include <cstring>
using namespace jkgm;

static std::array<char,32> name(char const *s) {
    std::array<char,32> n{};
    std::strncpy(n.data(),s,n.size()-1);
    return n;
}
int main() {
    sprite_type_registry types;
    std::array<unsigned char,0xac> pooled_sprite{};
    auto const grass=name("gen_grass07a.spr");
    std::memcpy(pooled_sprite.data(),grass.data(),grass.size());
    void const *sprite=pooled_sprite.data();
    types.remember(sprite,4,reinterpret_cast<char const *>(pooled_sprite.data()));
    // sithSprite_Load overwrites +0x20 with index 11; the authored type
    // must remain 4 so the sprite preserves Up, not Look.
    uint32_t world_sprite_index=11;
    std::memcpy(pooled_sprite.data()+0x20,&world_sprite_index,4);
    uint32_t saved=0;
    assert(types.lookup(sprite,reinterpret_cast<char const *>(pooled_sprite.data()),saved));
    assert(saved==4);
    assert((saved&sprite_preserve_up)!=0);
    assert((saved&sprite_preserve_look)==0);

    // A zero-type rock in the same world can have index 12.
    auto const rock=name("gen_rock01a.spr");
    std::memcpy(pooled_sprite.data(),rock.data(),rock.size());
    assert(!types.lookup(sprite,reinterpret_cast<char const *>(pooled_sprite.data()),saved));
    types.remember(sprite,0,reinterpret_cast<char const *>(pooled_sprite.data()));
    uint32_t rock_world_index=12;
    std::memcpy(pooled_sprite.data()+0x20,&rock_world_index,4);
    assert(types.lookup(sprite,reinterpret_cast<char const *>(pooled_sprite.data()),saved));
    assert(saved==0);

    // A resource is removed when freed and the same pointer can be reused.
    types.forget(sprite);
    assert(!types.lookup(sprite,reinterpret_cast<char const *>(pooled_sprite.data()),saved));
    auto const tree=name("gen_tree2.spr");
    std::memcpy(pooled_sprite.data(),tree.data(),tree.size());
    types.remember(sprite,4,reinterpret_cast<char const *>(pooled_sprite.data()));
    uint32_t tree_index=16;
    std::memcpy(pooled_sprite.data()+0x20,&tree_index,4);
    assert(types.lookup(sprite,reinterpret_cast<char const *>(pooled_sprite.data()),saved));
    assert(saved==4);
    assert(types.size()==1);
    types.clear();
    assert(types.size()==0);
    assert(!types.lookup(sprite,reinterpret_cast<char const *>(pooled_sprite.data()),saved));
}
