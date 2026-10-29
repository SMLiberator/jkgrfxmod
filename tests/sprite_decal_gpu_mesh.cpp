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
    assert(!group.nonplanar);
    assert((group.face_type&0x1U)==0U); // one-sided; no bleed through slabs
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
    // The clipped decal supplies interpolated projector coordinates and
    // angle opacity through otherwise unused fully-lit vertex attributes.
    assert(eq(gpu.geometry->vertices[0].fax,0.0f));
    assert(eq(gpu.geometry->vertices[0].fay,1.0f));
    assert(eq(gpu.geometry->vertices[0].faz,0.4f));
    assert(eq(gpu.geometry->vertices[0].face_extra,1.0f));
    assert(eq(decal_smooth_opacity(-0.25f,0.50f,1.0f),1.0f));
    assert(eq(decal_smooth_opacity(-0.25f,0.50f,-1.0f),0.0f));
    // Steep receivers should wrap and lose opacity gradually, instead of
    // an all-or-nothing polygon-angle rejection.
    decal_world_triangle steep{};
    steep.vertices[0].world={{0.0f,0.0f,-0.25f}};
    steep.vertices[1].world={{0.0f,0.20f,-0.25f}};
    steep.vertices[2].world={{0.0f,0.0f,0.25f}};
    for(auto &vertex:steep.vertices) {
        vertex.u=vertex.world[0];
        vertex.v=vertex.world[2];
        vertex.depth=vertex.world[1];
    }
    decal_world_scan steep_scan{};
    steep_scan.valid_world=true;
    steep_scan.triangles={steep};
    sprite_decal_mesh wrapped{};
    assert(make_sprite_decal_gpu_mesh(
        steep_scan,projector,0x123456U,t,1.0f,wrapped,true));
    assert(wrapped.geometry->vertices[0].face_extra>0.0f);
    assert(wrapped.geometry->vertices[0].face_extra<0.5f);
    assert(wrapped.geometry->vertices[0].fax==0.5f);
    sprite_decal_mesh moving_wrap{};
    assert(make_sprite_decal_gpu_mesh(
        steep_scan,projector,0x123456U,t,1.0f,moving_wrap,true,true));
    assert(moving_wrap.geometry->vertices[0].face_extra>0.5f);
    assert(moving_wrap.geometry->groups[0].face_type==0U);
    assert(decal_smooth_opacity(-0.98f,0.50f,-0.6f)>0.0f);
    assert(eq(decal_smooth_opacity(-0.98f,0.50f,-1.0f),0.0f));
    // Face boundary feathering is fragment-based: a wholly clipped quad
    // whose corners touch the volume walls must not disappear entirely.
    assert(gpu.geometry->vertices[0].face_extra==1.0f);
    // Two 3DO faces meeting at a corner must share the averaged normal
    // where their projected coordinates coincide. The angle fade should
    // graduate across the perpendicular side instead of ending at a seam.
    decal_world_triangle side{};
    side.vertices[0].world={{0.5f,0.1f,-0.5f}};
    side.vertices[1].world={{0.5f,0.1f,0.5f}};
    side.vertices[2].world={{0.5f,0.2f,-0.5f}};
    for(auto &vertex:side.vertices) {
        vertex.u=vertex.world[0];
        vertex.v=vertex.world[2];
        vertex.depth=vertex.world[1];
    }
    decal_world_scan corner_scan{};
    corner_scan.valid_world=true;
    corner_scan.triangles={a,b,side};
    sprite_decal_mesh corner_mesh{};
    assert(make_sprite_decal_gpu_mesh(
        corner_scan,projector,0x123456U,t,1.0f,corner_mesh,true));
    auto const &corner_vertices=corner_mesh.geometry->vertices;
    assert(corner_vertices.size()==9);
    assert(corner_vertices[6].face_extra>
           corner_vertices[8].face_extra);

    // A real 90-degree 3DO corner, sharing two vertices across front and
    // side polygons. Under face-normal UV wrapping the two faces sampled
    // DIFFERENT texels at the exact same world position (a hard painted
    // band/seam). Per-corner normal wrapping must preserve both UVs.
    decal_world_triangle front_corner{},side_corner{};
    front_corner.vertices[0].world={{0.0f,0.1f,-0.2f}};
    front_corner.vertices[1].world={{0.3f,0.1f,-0.2f}};
    front_corner.vertices[2].world={{0.3f,0.1f,0.2f}};
    side_corner.vertices[0].world=front_corner.vertices[1].world;
    side_corner.vertices[1].world={{0.3f,0.20f,-0.2f}};
    side_corner.vertices[2].world=front_corner.vertices[2].world;
    for(auto *tri:{&front_corner,&side_corner}) {
        for(auto &vertex:tri->vertices) {
            vertex.u=vertex.world[0];
            vertex.v=vertex.world[2];
            vertex.depth=vertex.world[1];
        }
    }
    decal_world_scan right_angle{};
    right_angle.valid_world=true;
    right_angle.triangles={front_corner,side_corner};
    sprite_decal_mesh smooth_corner{};
    assert(make_sprite_decal_gpu_mesh(
        right_angle,projector,0x123456U,t,1.0f,smooth_corner,true,true));
    auto const &cv=smooth_corner.geometry->vertices;
    assert(cv.size()==6U);
    // Front #1 and side #0: same world point, same sampled decal texel.
    assert(eq(cv[1].u,cv[3].u) && eq(cv[1].v,cv[3].v));
    // Front #2 and side #2: same second shared world point and texel.
    assert(eq(cv[2].u,cv[5].u) && eq(cv[2].v,cv[5].v));
    assert(eq(cv[1].face_extra,cv[3].face_extra));
    assert(eq(cv[2].face_extra,cv[5].face_extra));
    // Yet the perpendicular side has real texture extent along projector
    // depth, not a degenerate single-color line.
    assert(std::fabs(cv[4].u-cv[3].u)>0.05f);
    // Standalone planar mapping must remain unchanged when wrapping off.
    auto planar=decal_wrapped_projector_uv(
        front_corner.vertices[1],projector,{{0,-1,0}},false);
    assert(eq(planar[0],0.8f) && eq(planar[1],0.7f));
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
