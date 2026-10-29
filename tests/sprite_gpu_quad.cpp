#include "renderer/sprite_gpu_quad.hpp"
#include <cassert>
#include <cmath>
using namespace jkgm;
inline bool near(float x,float y){return std::fabs(x-y)<0.0001f;}
int main() {
    sprite_decal_texture_coords uv{0.5f,63.5f,0.5f,31.5f};
    auto mesh=make_gpu_sprite_quad(0x12340,uv,0);
    assert(mesh && mesh->vertices.size()==4 && mesh->indices.size()==6);
    assert(mesh->vertices[0].u==uv.left && mesh->vertices[0].v==uv.bottom);
    assert(mesh->vertices[2].u==uv.right && mesh->vertices[2].v==uv.top);
    assert(mesh->groups[0].face_type==1U && mesh->groups[0].count==6);
    assert(!make_gpu_sprite_quad(0,uv,0));
    auto solid=make_gpu_sprite_quad(0x12340,uv,1,true);
    assert(solid && solid->groups[0].solid_color);
    assert(solid->material_refs.empty());
    assert(solid->solid_material_refs.size()==1);
    assert(!mesh->groups[0].solid_color);
    gpu_3do_matrix34 id{{1,0,0,0,1,0,0,0,1,0,0,0}},pose{};
    float size[4]{4,6,2,3},offset[3]{0,0,0};
    assert(make_gpu_sprite_pose(sprite_default,id,id,size,offset,1,1,0,pose));
    assert(near(pose[0],2) && near(pose[8],3));
    assert(make_gpu_sprite_pose(sprite_default,id,id,size,offset,2,0.5f,90,pose));
    assert(near(pose[2],4) && near(pose[6],-1.5f));
    offset[0]=2;offset[2]=1;
    assert(make_gpu_sprite_pose(sprite_default,id,id,size,offset,1,1,0,pose));
    assert(near(pose[9],2) && near(pose[11],1));
    assert(!make_gpu_sprite_pose(sprite_project_decal,id,id,size,offset,1,1,0,pose));
}
