#include "renderer/sprite_orientation.hpp"
#include <cassert>
#include <cmath>
#include <cstdint>
using namespace jkgm;
static bool eq(sprite_vec3 const &a,sprite_vec3 const &b) {
    for(int i=0;i<3;++i)
        if(std::fabs(a[i]-b[i])>0.001f)return false;
    return true;
}
int main() {
    sprite_axes camera{},thing{},out{};
    thing.right={{0,1,0}};
    thing.look={{-1,0,0}};
    thing.up={{0,0,1}};
    // Type 0 and flare type 4 face the camera. Unknown future types
    // fail safely to JK's standard orientation, not accidental bit flags.
    for(uint32_t t:{0U,4U,5U,8U,32U,64U,127U}) {
        assert(compute_sprite_axes(t,thing,camera,out));
        assert(eq(out.right,camera.right));
        assert(eq(out.look,camera.look));
        assert(eq(out.up,camera.up));
    }
    assert(sprite_is_flare(4U) && !sprite_is_flare(64U));
    assert(sprite_is_decal(3U) && !sprite_is_decal(32U));
    assert(!sprite_needs_custom_axes(0U));
    assert(sprite_needs_custom_axes(1U));
    assert(sprite_needs_custom_axes(2U));
    assert(sprite_needs_custom_axes(3U));
    assert(!sprite_needs_custom_axes(4U));
    // Type 1 is an upright, cylindrical billboard.
    assert(compute_sprite_axes(sprite_billboard,thing,camera,out));
    assert(eq(out.up,thing.up));
    assert(eq(out.look,camera.look));
    // Type 2 / 3 retain the Thing's complete spatial orientation.
    for(uint32_t t:{sprite_fixed,sprite_project_decal}) {
        assert(compute_sprite_axes(t,thing,camera,out));
        assert(eq(out.right,thing.right));
        assert(eq(out.look,thing.look));
        assert(eq(out.up,thing.up));
    }
    sprite_axes bad=thing;
    bad.up={{0,0,0}};
    assert(!compute_sprite_axes(sprite_fixed,bad,camera,out));
    assert(!compute_sprite_axes(sprite_project_decal,bad,camera,out));
    // Look nearly parallel to preserved Up: use a stable camera tangent.
    camera.right={{0,1,0}};
    camera.look={{0.00001f,0,1}};
    assert(compute_sprite_axes(sprite_billboard,thing,camera,out));
    assert(eq(out.up,thing.up));
    assert(eq(out.right,camera.right));
    // Tilted Up with a rolled camera must not invert the billboard.
    camera.right={{-1,0,0}};
    camera.look={{0,1,0}};
    camera.up={{0,0,-1}};
    sprite_axes tilted=thing;
    tilted.up={{0,0.6f,0.8f}};
    assert(compute_sprite_axes(sprite_billboard,tilted,camera,out));
    assert(eq(out.up,tilted.up));
    assert(sprite_dot(out.look,camera.look)>0.75f);
    assert(std::fabs(sprite_dot(out.look,out.up))<0.001f);
}
