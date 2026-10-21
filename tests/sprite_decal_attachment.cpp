#include "renderer/sprite_decal_attachment.hpp"
#include <cassert>
#include <cmath>
#include <vector>

using namespace jkgm;
static bool near(float a,float b) { return std::fabs(a-b)<1.e-5f; }

int main() {
    sprite_axes original{};
    sprite_axes aligned{};
    // Projectile's ordinary image plane is upright (Right X / Up Z).
    // When attached to a horizontal floor, it must project into -Z.
    assert(sprite_decal_attachment_axes(original,{{0,0,1}},aligned));
    assert(near(aligned.look[0],0) && near(aligned.look[1],0) &&
           near(aligned.look[2],-1));
    assert(near(aligned.right[0],1) && near(aligned.up[1],1));

    decal_projector projector{};
    assert(make_sprite_decal_projector(
        aligned,{{0,0,0}},{{0,0,0}},1,1,1,1,0,projector));
    sprite_decal_center_around_attached_face(projector,{{0,0,1}});
    assert(near(projector.origin[2],0.125f));
    assert(near(projector.depth,0.25f));

    // Native Thing points exactly ON the floor. It still clips a valid
    // decal on the floor rather than drawing its original upright quad.
    std::vector<decal_vec3> ground{
        decal_vec3{{-1,-1,0}},decal_vec3{{1,-1,0}},
        decal_vec3{{1,1,0}},decal_vec3{{-1,1,0}}
    };
    auto clipped=clip_projected_decal(projector,ground,{{0,0,1}});
    assert(clipped.size()>=3);

    // Avoid depending on alignment of a specific Thing right axis to
    // the surface normal. The Up fallback still produces a valid basis.
    sprite_axes vertical{};
    vertical.right={{0,0,1}};
    vertical.look={{1,0,0}};
    vertical.up={{0,1,0}};
    assert(sprite_decal_attachment_axes(vertical,{{0,0,1}},aligned));
    assert(valid_decal_projector(
        decal_projector{{{0,0,0}},aligned.right,aligned.up,
                         aligned.look,1,1,0.25f}));
    assert(!sprite_decal_attachment_axes(
        original,{{0,0,0}},aligned));
}
