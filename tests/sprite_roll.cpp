#include "renderer/sprite_quad_transform.hpp"
#include "renderer/sprite_instance_info.hpp"
#include <cassert>
#include <cmath>
#include <cstdint>
using namespace jkgm;
static bool eq(float a,float b) {return std::fabs(a-b)<0.001f;}
int main() {
    float quad[12]={-1,0,-1, 1,0,-1, 1,0,1, -1,0,1};
    assert(transform_sprite_camera_quad(quad,1,1,0));
    assert(eq(quad[0],-1) && eq(quad[11],1));
    assert(transform_sprite_camera_quad(quad,2,0.5f,90));
    assert(eq(quad[0],0.5f) && eq(quad[2],-2.0f));
    assert(eq(quad[3],0.5f) && eq(quad[5],2.0f));
    assert(eq(quad[6],-0.5f) && eq(quad[8],2.0f));
    assert(eq(quad[9],-0.5f) && eq(quad[11],-2.0f));
    float bad[12]={-1,0,-1, 1,0,-1, 1,0,1, -1,0,1};
    assert(!transform_sprite_camera_quad(bad,1,-1,0));
    assert(eq(bad[0],-1));
    auto thing=reinterpret_cast<void const *>(static_cast<uintptr_t>(0x1234000));
    sprite_instance_registry reg;
    assert(reg.set_roll(thing,77,360,1000,2.0f));
    assert(eq(reg.sample_roll(thing,77,2000),180));
    assert(eq(reg.sample_roll(thing,77,3000),360));
    assert(eq(reg.sample_roll(thing,78,3000),0)); // reused Thing
    assert(reg.set_roll(thing,77,540,2000,1.0f)); // mid-flight re-target
    assert(eq(reg.sample_roll(thing,77,2500),360));
    assert(reg.set(thing,77,{2,2,0.5f},1000,1));
    assert(eq(reg.sample_roll(thing,77,2500),360)); // separate channel
    reg.forget(thing);
    assert(eq(reg.sample_roll(thing,77,3000),0));
}
