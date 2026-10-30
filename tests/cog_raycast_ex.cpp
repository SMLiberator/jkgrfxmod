#include "../renderer/cog_raycast_ex_geometry.hpp"
#include <cassert>
#include <cmath>
int main() {
    using namespace jkgm;
    decal_world_geometry world{};
    world.valid_world=true;
    decal_world_surface_geometry wall{};
    wall.surface_index=42;
    wall.normal={{0,0,-1}};
    wall.polygon={{{0,0,5}},{{0,1,5}},{{1,1,5}},{{1,0,5}}};
    wall.bounds_min={{0,0,5}};
    wall.bounds_max={{1,1,5}};
    world.receivers.push_back(wall);
    world.acceleration.build(world.receivers);
    auto hit=cog_ray_ex_cast_world(world,{{0.5f,0.5f,0}},
                                     {{0,0,1}},10);
    assert(hit.hit && hit.surface_index==42);
    assert(std::fabs(hit.distance-5)<1.e-5f);
    assert(std::fabs(hit.normal[2]+1)<1.e-5f);
    assert(!cog_ray_ex_cast_world(world,{{0.5f,0.5f,0}},
                                     {{0,0,1}},4.9f).hit);
    assert(!cog_ray_ex_cast_world(world,{{2,2,0}},
                                     {{0,0,1}},10).hit);
    // RayCastEx must obey the front-face rule, same as JK.
    assert(!cog_ray_ex_cast_world(world,{{0.5f,0.5f,10}},
                                     {{0,0,-1}},10).hit);
    // A nearer surface replaces the farther one irrespective of list order.
    auto nearer=wall;
    nearer.surface_index=17;
    nearer.bounds_min[2]=nearer.bounds_max[2]=3;
    for(auto &v:nearer.polygon)v[2]=3;
    world.receivers.push_back(nearer);
    world.acceleration.build(world.receivers);
    hit=cog_ray_ex_cast_world(world,{{0.5f,0.5f,0}},{{0,0,1}},10);
    assert(hit.hit && hit.surface_index==17 &&
           std::fabs(hit.distance-3)<1.e-5f);
    assert(cog_ray_ex_near_sphere({{0,0,0}},{{1,0,0}},10,
                                  {{5,0.5f,0}},1));
    assert(!cog_ray_ex_near_sphere({{0,0,0}},{{1,0,0}},10,
                                   {{5,5,0}},1));
    assert(cog_ray_ex_near_sphere({{0,0,0}},{{1,0,0}},10,
                                  {{5,5,0}},0));
}
