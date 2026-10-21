#include "renderer/sprite_decal_projector.hpp"
#include <cassert>
#include <cmath>
using namespace jkgm;
static bool near(float a,float b) {return std::fabs(a-b)<0.0001f;}
int main() {
    sprite_axes axes{};
    decal_projector p{};
    assert(make_sprite_decal_projector(
        axes,{{1,2,3}},{{2,3,4}},1.0f,0.5f,2.0f,3.0f,
        90.0f,p));
    assert(near(p.origin[0],3) && near(p.origin[1],5) &&
           near(p.origin[2],7.125f));
    assert(near(p.width,2) && near(p.height,1.5f));
    assert(near(p.depth,sprite_decal_projection_depth));
    assert(near(p.right[0],0) && near(p.right[1],1));
    assert(near(p.up[0],-1) && near(p.up[2],0));
    assert(near(p.look[2],-1));
    // A complete 360-degree roll returns to original orientation.
    assert(make_sprite_decal_projector(
        axes,{{0,0,0}},{{0,0,0}},2.0f,2.0f,1.0f,1.0f,
        360.0f,p));
    assert(near(p.right[0],1) && near(p.up[1],1) &&
           near(p.look[2],-1) && near(p.origin[2],0.125f));
    // Malformed scales and projectors must not reach geometry clipping.
    assert(!make_sprite_decal_projector(
        axes,{{0,0,0}},{{0,0,0}},2.0f,2.0f,0.0f,1.0f,
        0,p));
    auto bad_axes=axes;
    bad_axes.look={{0,0,0}};
    assert(!make_sprite_decal_projector(
        bad_axes,{{0,0,0}},{{0,0,0}},1,1,1,1,0,p));
    decal_vertex v{};
    v.world={{0,0.1f,0}};
    v.u=0.25f;v.v=-0.25f;v.depth=0.1f;
    offset_sprite_decal_receiver(v,{{0,-1,0}});
    assert(near(v.world[1],0.1f-sprite_decal_surface_bias));
    assert(near(v.u,0.25f) && near(v.depth,0.1f));
}
