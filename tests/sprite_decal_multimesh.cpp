#include "renderer/sprite_decal_multimesh.hpp"
#include "renderer/sprite_decal_gpu_mesh.hpp"
#include <cassert>
#include <cmath>
#include <utility>
#include <vector>

using namespace jkgm;
static bool close(float a,float b,float eps=0.0002f) {
    return std::fabs(a-b)<eps;
}
static decal_world_geometry surface_at(float y) {
    decal_world_geometry world{};
    world.valid_world=true;
    decal_world_surface_geometry f{};
    // Face the incoming +Y projectile, without creating a reversed slab.
    f.normal={{0,-1,0}};
    f.polygon={{{-0.1f,y,-0.1f}},{{0.1f,y,-0.1f}},
               {{0.1f,y,0.1f}},{{-0.1f,y,0.1f}}};
    f.bounds_min={{-0.1f,y,-0.1f}};
    f.bounds_max={{0.1f,y,0.1f}};
    world.receivers.push_back(f);
    world.surface_count=1;
    world.acceleration.build(world.receivers);
    return world;
}
int main() {
    // Two visible 3DO meshes in ONE Thing, separated along the projector's
    // depth axis. The old per-node depth box hits the chest, but misses the
    // offset belly completely and leaves a rectangular blood cutoff.
    decal_projector projector{};
    projector.origin={{0,-0.04f,0}};
    projector.depth=0.08f;
    projector.width=0.30f;
    projector.height=0.30f;
    auto chest=surface_at(0.0f);
    auto belly=surface_at(-0.09f);
    assert(!project_cached_decal_world(
        chest,projector,2048U,true,true).triangles.empty());
    assert(project_cached_decal_world(
        belly,projector,2048U,true,true).triangles.empty());
    std::vector<decal_world_geometry> pieces{chest,belly};
    assert(fit_moving_decal_multimesh_depth(projector,pieces));
    assert(projector.depth>0.08f && projector.depth<=0.25f);
    assert(projector.origin[1]<-0.04f);
    assert(!project_cached_decal_world(
        chest,projector,2048U,true,true).triangles.empty());
    assert(!project_cached_decal_world(
        belly,projector,2048U,true,true).triangles.empty());
    // Don't expand a single receiver, or extend through the rear face of
    // a thin slab whose normal points with the incoming direction.
    decal_projector single{};
    single.origin={{0,-0.04f,0}};
    single.depth=0.08f;
    single.width=single.height=0.30f;
    assert(!fit_moving_decal_multimesh_depth(
        single,std::vector<decal_world_geometry>{chest}));
    auto underside=surface_at(-0.09f);
    underside.receivers[0].normal={{0,1,0}};
    std::vector<decal_world_geometry> opposite{chest,underside};
    assert(!fit_moving_decal_multimesh_depth(single,opposite));
    assert(close(single.depth,0.08f));

    // Separately rendered meshes can share a seam without sharing a mesh
    // or a native node. Their UVs and alpha must agree at the shared
    // corners, even though each triangle uses a DIFFERENT face normal.
    decal_projector corner_proj{};
    corner_proj.origin={{0,-0.10f,0}};
    corner_proj.depth=0.25f;
    corner_proj.width=corner_proj.height=0.30f;
    decal_world_triangle front{},side{};
    front.vertices[0].world={{-0.1f,0,-0.1f}};
    front.vertices[1].world={{0.1f,0,-0.1f}};
    front.vertices[2].world={{0.1f,0,0.1f}};
    side.vertices[0].world=front.vertices[1].world;
    side.vertices[1].world={{0.1f,0.08f,-0.1f}};
    side.vertices[2].world=front.vertices[2].world;
    for(auto *tri:{&front,&side})
        for(auto &v:tri->vertices) {
            v.u=v.world[0];
            v.v=v.world[2];
            v.depth=v.world[1]-corner_proj.origin[1];
        }
    decal_world_scan fscan{},sscan{};
    fscan.valid_world=sscan.valid_world=true;
    fscan.triangles.push_back(front);
    sscan.triangles.push_back(side);
    sprite_decal_texture_coords uv{0.5f,63.5f,0.5f,63.5f};
    sprite_decal_mesh f_old{},s_old{},f_new{},s_new{};
    assert(make_sprite_decal_gpu_mesh(
        fscan,corner_proj,0x123456U,uv,1,f_old,true,true));
    assert(make_sprite_decal_gpu_mesh(
        sscan,corner_proj,0x123456U,uv,1,s_old,true,true));
    auto const &f0=f_old.geometry->vertices[1];
    auto const &s0=s_old.geometry->vertices[0];
    assert(std::fabs(f0.u-s0.u)>0.05f); // actual old inter-node seam
    decal_normal_map shared{};
    assert(accumulate_sprite_decal_normals(fscan,corner_proj,shared));
    assert(accumulate_sprite_decal_normals(sscan,corner_proj,shared));
    assert(make_sprite_decal_gpu_mesh(
        fscan,corner_proj,0x123456U,uv,1,f_new,true,true,&shared));
    assert(make_sprite_decal_gpu_mesh(
        sscan,corner_proj,0x123456U,uv,1,s_new,true,true,&shared));
    auto const &fv=f_new.geometry->vertices;
    auto const &sv=s_new.geometry->vertices;
    assert(close(fv[1].u,sv[0].u) && close(fv[1].v,sv[0].v));
    assert(close(fv[2].u,sv[2].u) && close(fv[2].v,sv[2].v));
    assert(close(fv[1].face_extra,sv[0].face_extra));
    assert(close(fv[2].face_extra,sv[2].face_extra));
    assert(f_new.geometry->groups[0].face_type==0U);
    assert(s_new.geometry->groups[0].face_type==0U);
    return 0;
}
