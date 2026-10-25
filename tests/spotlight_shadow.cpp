#include "renderer/spotlight_shadow.hpp"
#include <cassert>
#include <cmath>
using namespace jkgm;

static bool near(float x,float y,float epsilon=0.0001f)
{
    return std::fabs(x-y)<=epsilon;
}
static decal_world_surface_geometry polygon(
    uint32_t id,float x,float y)
{
    decal_world_surface_geometry s{};
    s.surface_index=id;
    s.polygon={{{x-0.25f,y,-0.25f}},
               {{x+0.25f,y,-0.25f}},
               {{x+0.25f,y,0.25f}},
               {{x-0.25f,y,0.25f}}};
    s.bounds_min={{x-0.25f,y,-0.25f}};
    s.bounds_max={{x+0.25f,y,0.25f}};
    return s;
}
int main()
{
    spotlight_instance s{};
    s.origin={{0,0,0}};
    s.right={{1,0,0}};
    s.forward={{0,1,0}};
    s.up={{0,0,1}};
    s.width=4.0f;s.height=2.0f;s.range=10.0f;
    spotlight_shadow_projection p{};
    assert(make_spotlight_shadow_projection(s,p));
    assert(p.near_distance>0.0f && p.near_distance<s.range);
    auto c=spotlight_shadow_clip(p,{{0,5,0}});
    assert(near(c.w,5.0f) && near(c.x,0.0f) && near(c.y,0.0f));
    assert(near(0.5f*(c.z/c.w+1.0f),spotlight_shadow_depth(p,5)));
    assert(near(spotlight_shadow_depth(p,p.near_distance),0.0f));
    assert(near(spotlight_shadow_depth(p,p.range),1.0f));
    // This point is exactly the right edge of the existing MAT cookie.
    c=spotlight_shadow_clip(p,{{1,5,0}});
    assert(near(c.x/c.w,1.0f));
    c=spotlight_shadow_clip(p,{{0,5,0.5f}});
    assert(near(c.y/c.w,1.0f));
    auto box=spotlight_shadow_volume_bounds(p);
    assert(box.low[0]<-2.0f && box.high[0]>2.0f);
    assert(box.low[1]<0.0f && box.high[1]>10.0f);
    assert(box.low[2]<-1.0f && box.high[2]>1.0f);

    // The per-world cache is built once, independently of the camera.
    decal_world_geometry world{};
    spotlight_shadow_static_mesh invalid{};
    assert(!build_spotlight_shadow_static_mesh(world,invalid));
    world.valid_world=true;
    world.receivers.push_back(polygon(0,0.0f,5.0f));
    world.receivers.push_back(polygon(1,25.0f,5.0f));
    world.receivers.push_back(polygon(2,-1.0f,7.0f));
    world.acceleration.build(world.receivers);
    spotlight_shadow_static_mesh mesh{};
    assert(build_spotlight_shadow_static_mesh(world,mesh));
    assert(mesh.vertices.size()==12);
    assert(mesh.indices.size()==18);
    assert(mesh.surfaces.size()==3);
    assert(mesh.surfaces[0].first_index==0 && mesh.surfaces[0].count==6);
    assert(mesh.surfaces[1].first_index==6 && mesh.surfaces[1].count==6);
    assert(mesh.surfaces[2].first_index==12 && mesh.surfaces[2].count==6);
    assert(mesh.indices[0]==0 && mesh.indices[1]==1 && mesh.indices[2]==2);
    std::vector<spotlight_shadow_surface_range> candidates;
    query_spotlight_shadow_static_mesh(world,mesh,p,candidates);
    assert(candidates.size()==2);
    assert(candidates[0].first_index==0);
    assert(candidates[1].first_index==12);
    // Moving the projector queries the BVH without rebuilding/uploading mesh.
    p.origin={{25,0,0}};
    query_spotlight_shadow_static_mesh(world,mesh,p,candidates);
    assert(candidates.size()==1);
    assert(candidates[0].first_index==6);
    // Invalid transforms do not produce a shadow projection.
    s.forward={{0,0,0}};
    assert(!make_spotlight_shadow_projection(s,p));
    return 0;
}
