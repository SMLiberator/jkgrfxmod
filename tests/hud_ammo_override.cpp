#include "renderer/hud_ammo_override.hpp"
#include <cassert>
int main()
{
    using namespace jkgm::hud_ammo;
    assert(default_bin(2)==11 && default_bin(3)==11);
    assert(default_bin(4)==4 && default_bin(5)==12);
    assert(default_bin(6)==12 && default_bin(7)==15);
    assert(default_bin(8)==8 && default_bin(9)==12);
    assert(default_bin(0)==-1 && default_bin(1)==-1);
    assert(default_bin(10)==-1 && default_bin(25)==-1);
    selection state{};
    state.observe(0x10000,0x20000,2);
    assert(state.get()==11 && !state.overridden);
    state.set(25);
    assert(state.get()==25 && state.overridden);
    state.observe(0x10000,0x20000,2); // no change
    assert(state.get()==25);
    state.set(-1); // hide, not restore
    assert(state.overridden && state.get()==-1);
    state.observe(0x10000,0x20000,3); // weapon change resets
    assert(!state.overridden && state.get()==11);
    state.set(30);
    state.observe(0x10000,0x30000,3); // local player change
    assert(!state.overridden && state.get()==11);
    state.set(42);
    state.observe(0x40000,0x30000,3); // world change
    assert(!state.overridden && state.get()==11);
    return 0;
}
