#include "renderer/sprite_instance_info.hpp"
#include <cassert>
#include <cmath>
#include <cstdint>
using namespace jkgm;
static bool eq(float a,float b) {return std::fabs(a-b)<0.0001f;}
int main() {
    sprite_instance_registry reg;
    auto thing=reinterpret_cast<void const *>(static_cast<uintptr_t>(0x1234000));
    auto other=reinterpret_cast<void const *>(static_cast<uintptr_t>(0x1235000));
    auto v=reg.sample(thing,1,100);
    assert(eq(v.width,1) && eq(v.height,1) && eq(v.opacity,1));
    assert(reg.set(thing,1,{2,0.5f,0.25f},100,2.0f));
    v=reg.sample(thing,1,1100);
    assert(eq(v.width,1.5f) && eq(v.height,0.75f) && eq(v.opacity,0.625f));
    assert(reg.set(other,2,{4,4,0},100,0));
    auto u=reg.sample(other,2,1100);
    assert(eq(u.width,4) && eq(u.opacity,0));
    v=reg.sample(thing,1,2100);
    assert(eq(v.width,2) && eq(v.height,0.5f) && eq(v.opacity,0.25f));
    // Re-targeting during an animation begins from the current interpolated state.
    assert(reg.set(thing,1,{3,1,1},1100,1.0f));
    v=reg.sample(thing,1,1600);
    assert(eq(v.width,2.25f) && eq(v.height,0.875f) && eq(v.opacity,0.8125f));
    // Stale Thing pointers cannot transfer an old instance's values to a new signature.
    v=reg.sample(thing,999,1600);
    assert(eq(v.width,1) && eq(v.opacity,1));
    assert(!reg.set(thing,1,{-1,1,1},0,0));
    assert(!reg.set(thing,1,{1,1,2},0,0));
    assert(!reg.set(thing,1,{1,1,1},0,-1));
    assert(reg.set(thing,1,{0,0,0},0,0));
    v=reg.sample(thing,1,0);
    assert(eq(v.width,0) && eq(v.height,0) && eq(v.opacity,0));
    reg.forget(other);
    assert(reg.size()==1);
    reg.clear();
    assert(reg.size()==0);
    // DWORD time wraparound while a transition is active.
    assert(reg.set(thing,3,{2,2,1},0xfffffff0U,0.1f));
    v=reg.sample(thing,3,0x00000022U);
    assert(eq(v.width,1.5f));
}
