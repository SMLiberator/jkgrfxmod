#include "renderer/gpu_particle_cloud.hpp"
#include <cassert>
#include <cmath>
#include <vector>
int main() {
    jkgm::gpu_particle_dot textured{};
    textured.material=0x20000U;textured.cel=3;
    textured.view_position={{1,2,3}};
    textured.radius=0.25f;
    textured.light_mode=1;
    textured.textured=true;
    textured.width=64;textured.height=32;
    auto mesh=jkgm::make_gpu_particle_cloud({textured});
    assert(mesh && mesh->vertices.size()==4U);
    assert(mesh->groups.size()==1U && mesh->indices.size()==6U);
    assert(std::fabs(mesh->vertices[0].x-0.75f)<1.e-6f);
    assert(std::fabs(mesh->vertices[2].x-1.25f)<1.e-6f);
    assert(mesh->vertices[0].u==0.5f);
    assert(mesh->vertices[2].u==63.5f);
    assert(mesh->vertices[2].v==0.5f);
    assert(mesh->groups[0].cel==3);
    assert(mesh->vertices[0].light_mode==1.0f);
    textured.textured=false;textured.cel=4;
    auto diamond=jkgm::make_gpu_particle_cloud({textured});
    assert(diamond && diamond->groups[0].solid_color);
    assert(std::fabs(diamond->vertices[0].x-
                     (1.0f-0.25f*1.41421356f))<1.e-6f);
    assert(diamond->solid_material_refs.size()==1U);
    std::vector<jkgm::gpu_particle_dot> many(257,textured);
    assert(!jkgm::make_gpu_particle_cloud(many));
    textured.radius=0.0f;
    assert(!jkgm::make_gpu_particle_cloud({textured}));
}
