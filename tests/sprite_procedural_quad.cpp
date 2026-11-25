#include "renderer/sprite_gpu_quad.hpp"
#include <cassert>
using namespace jkgm;
int main() {
    sprite_decal_texture_coords uv{};
    uv.left=0.5f;uv.right=127.5f;uv.top=0.5f;uv.bottom=63.5f;
    auto classic=make_gpu_sprite_quad(0x1234U,uv,2U,false,3,2U,0.25f);
    auto generated=make_gpu_sprite_procedural_quad(0x1234U,uv,2U,false,3,2U,0.25f);
    assert(classic && generated && !classic->procedural_sprite);
    assert(generated->procedural_sprite);
    assert(generated->vertices.empty() && generated->indices.empty());
    assert(generated->groups.size()==1U);
    auto const &a=classic->groups[0],&b=generated->groups[0];
    assert(a.material==b.material && a.cel==b.cel);
    assert(a.face_type==b.face_type && a.count==b.count);
    assert(a.solid_color==b.solid_color);
    assert(a.bounds_min==b.bounds_min && a.bounds_max==b.bounds_max);
    assert(classic->material_refs==generated->material_refs);
    assert(generated->procedural_sprite_uv[0]==uv.left);
    assert(generated->procedural_sprite_uv[1]==uv.right);
    assert(generated->procedural_sprite_uv[2]==uv.top);
    assert(generated->procedural_sprite_uv[3]==uv.bottom);
    assert(generated->procedural_sprite_extra_light==0.25f);
    assert(generated->procedural_sprite_light_mode==1.0f);
    auto solid=make_gpu_sprite_procedural_quad(0x1234U,uv,0U,true,2,0U,0.0f);
    assert(solid && solid->groups[0].solid_color);
    assert(solid->solid_material_refs.size()==1U && solid->material_refs.empty());
    assert(solid->procedural_sprite_light_mode==0.0f);
    uv.right=uv.left;
    assert(!make_gpu_sprite_procedural_quad(0x1234U,uv,0U));
}
