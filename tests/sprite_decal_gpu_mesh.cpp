#include "renderer/sprite_decal_resource.hpp"
#include <cassert>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <vector>
using namespace jkgm;
static bool eq(float a,float b) {return std::fabs(a-b)<0.001f;}
int main() {
    // A two-triangle receiving wall, already clipped by the projector.
    decal_projector projector{};
    decal_world_scan scan{};
    scan.valid_world=true;
    scan.surfaces_seen=1;
    scan.receivers=1;
    decal_world_triangle a{};
    a.surface_index=2;
    a.vertices[0].world={{-0.5f,0.1f,-0.5f}};
    a.vertices[1].world={{0.5f,0.1f,-0.5f}};
    a.vertices[2].world={{0.5f,0.1f,0.5f}};
    for(int i=0;i<3;i++) {
        a.vertices[i].u=a.vertices[i].world[0];
        a.vertices[i].v=a.vertices[i].world[2];
        a.vertices[i].depth=0.1f;
    }
    auto b=a;
    b.vertices[1]=a.vertices[2];
    b.vertices[2].world={{-0.5f,0.1f,0.5f}};
    b.vertices[2].u=-0.5f;b.vertices[2].v=0.5f;
    scan.triangles={a,b};
    sprite_decal_texture_coords t{0.5f,63.5f,0.5f,31.5f};
    sprite_decal_mesh gpu{};
    assert(make_sprite_decal_gpu_mesh(scan,projector,0x123456U,t,0.6f,gpu));
    assert(gpu.geometry);
    assert(gpu.geometry->vertices.size()==6);
    assert(gpu.geometry->indices.size()==6);
    assert(gpu.geometry->groups.size()==1);
    auto const &group=gpu.geometry->groups[0];
    assert(group.material==0x123456U);
    assert(group.count==6);
    assert(group.nonplanar);
    assert(gpu.geometry->material_refs[0].first==0x123456U);
    assert(eq(gpu.opacity,0.6f));
    // Pixel-center conversion maps projected (0,0) to (0.5,0.5).
    assert(eq(gpu.geometry->vertices[0].u,0.5f));
    assert(eq(gpu.geometry->vertices[0].v,31.5f));
    assert(eq(gpu.geometry->vertices[2].u,63.5f));
    assert(eq(gpu.geometry->vertices[2].v,0.5f));
    assert(eq(gpu.geometry->vertices[3].u,0.5f));
    assert(eq(gpu.geometry->vertices[0].static_light,1.0f));
    assert(eq(gpu.geometry->vertices[0].cr,1.0f));
    assert(group.bounds_min[0]<=-0.5f);
    assert(group.bounds_max[2]>=0.5f);
    auto unchanged=gpu.geometry;
    assert(!make_sprite_decal_gpu_mesh(scan,projector,0,t,1,gpu));
    assert(gpu.geometry==unchanged);
    assert(!make_sprite_decal_gpu_mesh(scan,projector,0x123456U,t,-1,gpu));
    scan.truncated=true;
    assert(!make_sprite_decal_gpu_mesh(scan,projector,0x123456U,t,1,gpu));

    // Simulate the native rdSprite layout with independent address checking.
    constexpr uintptr_t base=0x10000000U;
    std::vector<uint8_t> memory(0x2000);
    auto write32=[&](uintptr_t at,uint32_t value) {
        std::memcpy(memory.data()+(at-base),&value,4);
    };
    uintptr_t sprite=base+0x100;
    uintptr_t mat=base+0x500;
    uintptr_t uvaddr=base+0x600;
    write32(sprite+0x7c,uint32_t(mat));
    write32(sprite+0x9c,uint32_t(uvaddr));
    write32(sprite+0x64,4);
    float uv[8]={0.5f,31.5f,63.5f,31.5f,
                 63.5f,0.5f,0.5f,0.5f};
    std::memcpy(memory.data()+uvaddr-base,uv,sizeof(uv));
    decal_native_read reader=[&](uintptr_t at,void *dst,size_t bytes) {
        if(at<base || at-base>memory.size() ||
           bytes>memory.size()-(at-base))return false;
        std::memcpy(dst,memory.data()+at-base,bytes);
        return true;
    };
    sprite_decal_resource resource{};
    assert(read_sprite_decal_resource(reader,sprite,resource));
    assert(resource.material==mat);
    assert(eq(resource.texcoords.right,63.5f));
    assert(eq(resource.texcoords.bottom,31.5f));
    uv[7]=123.0f; // malformed UV: right and left edges no longer agree
    std::memcpy(memory.data()+uvaddr-base,uv,sizeof(uv));
    assert(!read_sprite_decal_resource(reader,sprite,resource));
    write32(sprite+0x9c,0);
    assert(!read_sprite_decal_resource(reader,sprite,resource));
}
