#include "renderer/sprite_decal_projector.hpp"
#include <cassert>
#include <cmath>
#include <limits>
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
    assert(near(p.depth,sprite_decal_projection_max_depth));
    assert(near(p.right[0],0) && near(p.right[1],1));
    assert(near(p.up[0],-1) && near(p.up[2],0));
    assert(near(p.look[2],-1));
    // A complete 360-degree roll returns to original orientation.
    assert(make_sprite_decal_projector(
        axes,{{0,0,0}},{{0,0,0}},2.0f,2.0f,1.0f,1.0f,
        360.0f,p));
    assert(near(p.right[0],1) && near(p.up[1],1) &&
           near(p.look[2],-1) && near(p.origin[2],0.125f));
    // Bullet holes use their authored dimensions, not the previous
    // fixed 0.25 JKU (2.5m) projection thickness.
    assert(make_sprite_decal_projector(
        axes,{{0,0,0}},{{0,0,0}},0.02f,0.04f,1,1,0,p));
    assert(near(p.width,0.02f) && near(p.height,0.04f));
    assert(near(p.depth,0.03f));
    assert(near(p.origin[2],0.015f));
    assert(near(sprite_decal_receiver_search_radius(p.depth),0.02f));
    // Per-Thing SetSpriteInfo width/height changes projection depth too.
    assert(make_sprite_decal_projector(
        axes,{{0,0,0}},{{0,0,0}},0.02f,0.04f,2,2,0,p));
    assert(near(p.depth,0.06f));
    assert(near(sprite_decal_receiver_search_radius(p.depth),0.035f));
    // Elongated, unusually large, and tiny sprite inputs stay bounded.
    assert(near(sprite_decal_projection_depth_for_size(0.0002f,0.0004f),
                sprite_decal_projection_min_depth));
    assert(near(sprite_decal_projection_depth_for_size(1.0f,1.0f),
                sprite_decal_projection_max_depth));
    assert(sprite_decal_projection_depth_for_size(
        std::numeric_limits<float>::infinity(),1.0f)==0.0f);
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
