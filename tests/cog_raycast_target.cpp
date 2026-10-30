#include "renderer/cog_raycast_target.hpp"
#include <cassert>
#include <initializer_list>
using jkgm::cog_raycast_eligible_thing_pair;
int main() {
    constexpr unsigned actor=2,weapon=3,item=5,cog=7,corpse=9,player=10;
    // Native SithCollision registers Item <-> Player, never Item <-> Actor
    // or Item <-> Weapon. The two participant orders are both significant.
    assert(cog_raycast_eligible_thing_pair(player,item));
    assert(cog_raycast_eligible_thing_pair(item,player));
    assert(!cog_raycast_eligible_thing_pair(actor,item));
    assert(!cog_raycast_eligible_thing_pair(item,actor));
    assert(!cog_raycast_eligible_thing_pair(weapon,item));
    assert(!cog_raycast_eligible_thing_pair(item,weapon));
    assert(!cog_raycast_eligible_thing_pair(item,item));
    assert(!cog_raycast_eligible_thing_pair(cog,item));
    // Corpses only collide with world, never with another Thing.
    for(unsigned source : {actor,weapon,item,cog,corpse,player}) {
        assert(!cog_raycast_eligible_thing_pair(source,corpse));
        assert(!cog_raycast_eligible_thing_pair(corpse,source));
    }
    // Leave all other types under native collision-table filtering.
    assert(cog_raycast_eligible_thing_pair(weapon,actor));
    assert(cog_raycast_eligible_thing_pair(actor,player));
    assert(cog_raycast_eligible_thing_pair(player,cog));
}
