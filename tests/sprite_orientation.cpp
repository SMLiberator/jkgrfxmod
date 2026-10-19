#include "renderer/sprite_orientation.hpp"
#include <cassert>
#include <cmath>
#include <cstdint>
using namespace jkgm;
static bool eq(sprite_vec3 const &a,sprite_vec3 const &b) {
    for(int i=0;i<3;i++) if(std::fabs(a[i]-b[i])>0.001f) return false;
    return true;
}
int main() {
    sprite_axes camera,thing,out;
    thing.right={{0,1,0}};
    thing.look={{-1,0,0}};
    thing.up={{0,0,1}};
    for(uint32_t legacy:{0U,1U,2U}) {
        assert(compute_sprite_axes(legacy,thing,camera,out));
        assert(eq(out.right,camera.right));
        assert(eq(out.look,camera.look));
        assert(eq(out.up,camera.up));
    }
    assert(compute_sprite_axes(sprite_preserve_up,thing,camera,out));
    assert(eq(out.up,thing.up));
    assert(eq(out.look,camera.look));
    assert(compute_sprite_axes(sprite_preserve_look,thing,camera,out));
    assert(eq(out.look,thing.look));
    assert(eq(out.right,thing.right));
    assert(compute_sprite_axes(sprite_preserve_right,thing,camera,out));
    assert(eq(out.right,thing.right));
    assert(eq(out.look,thing.look));
    assert(compute_sprite_axes(sprite_preserve_look|sprite_preserve_up,thing,camera,out));
    assert(eq(out.right,thing.right));
    assert(compute_sprite_axes(sprite_preserve_right|sprite_preserve_up,thing,camera,out));
    assert(eq(out.look,thing.look));
    assert(compute_sprite_axes(sprite_preserve_right|sprite_preserve_look,thing,camera,out));
    assert(eq(out.up,thing.up));
    assert(compute_sprite_axes(sprite_project_decal,thing,camera,out));
    assert(eq(out.right,thing.right));
    assert(eq(out.look,thing.look));
    assert(eq(out.up,thing.up));
    // Extreme camera orientation: project-to-plane must fall back to Thing look.
    camera.look={{0,0,1}};
    assert(compute_sprite_axes(sprite_preserve_up,thing,camera,out));
    assert(eq(out.up,thing.up));
    sprite_axes bad=thing;
    bad.up={{0,0,0}};
    assert(!compute_sprite_axes(sprite_project_decal,bad,camera,out));
}
