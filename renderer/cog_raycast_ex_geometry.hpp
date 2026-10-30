#pragma once
// Explicit COG-only precision ray. Pure math, no Sith collision hook.
// Static world polygons are read from the existing read-only decal snapshot;
// animated model triangles use the separate precise_type3_native_cache.
#include "precise_type3_raycast.hpp"
#include "sprite_decal_world_cache.hpp"
#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <limits>
#include <vector>

namespace jkgm {
struct cog_ray_ex_world_hit {
    bool hit=false;
    float distance=0.0f;
    std::array<float,3> normal{};
    uint32_t surface_index=0;
};

// Conservative Thing-level broadphase. A failed/unknown radius does not
// reject a model; callers can still test its real polygons.
inline bool cog_ray_ex_near_sphere(
    precise_type3::vec3 const &start,
    precise_type3::vec3 const &direction,float length,
    precise_type3::vec3 const &center,float radius)
{
    if(!precise_type3::finite(start) ||
       !precise_type3::finite(direction) ||
       !precise_type3::finite(center) ||
       !std::isfinite(radius) || radius<=0.0f)return true;
    auto const a=precise_type3::to_double(start);
    auto const d=precise_type3::to_double(direction);
    auto const c=precise_type3::to_double(center);
    double const t=std::max(0.0,std::min(double(length),
        precise_type3::dot(precise_type3::minus(c,a),d)));
    double squared=0;
    for(int k=0;k<3;++k) {
        double const delta=c[k]-(a[k]+t*d[k]);
        squared+=delta*delta;
    }
    // Allow modest authoring errors in collideSize, without turning the
    // high-precision ray into a broad swept sphere.
    double const bound=std::max(0.1,double(radius)*1.5+0.05);
    return squared<=bound*bound;
}

inline cog_ray_ex_world_hit cog_ray_ex_cast_world(
    decal_world_geometry const &world,
    precise_type3::vec3 const &origin,
    precise_type3::vec3 const &direction,float max_distance)
{
    cog_ray_ex_world_hit best{};
    if(!world.valid_world || !(max_distance>0.0f) ||
       !std::isfinite(max_distance) || !precise_type3::finite(origin) ||
       !precise_type3::finite(direction))return best;
    precise_type3::ray r{};
    r.start=precise_type3::to_double(origin);
    r.direction=precise_type3::to_double(direction);
    decal_vec3 lo{},hi{};
    for(int a=0;a<3;++a) {
        double const e=r.start[a]+r.direction[a]*max_distance;
        lo[a]=float(std::min(r.start[a],e));
        hi[a]=float(std::max(r.start[a],e));
        // Include the existing polygon edge tolerance.
        lo[a]-=0.0001f;
        hi[a]+=0.0001f;
    }
    std::vector<uint32_t> candidates;
    world.acceleration.query(world.receivers,lo,hi,candidates);
    double closest=max_distance;
    for(uint32_t i:candidates) {
        auto const &surface=world.receivers[i];
        if(!precise_type3::ray_intersects_bounds(
              r,surface.bounds_min,surface.bounds_max,closest))continue;
        auto const vertex=[&](size_t v) {
            return surface.polygon[v];
        };
        double hit_distance=0;
        if(!precise_type3::hit_face(
               r,surface.normal,surface.polygon.size(),vertex,
               closest,hit_distance))continue;
        double norm2=0;
        for(float x:surface.normal)norm2+=double(x)*x;
        if(!(norm2>1.e-20))continue;
        best.hit=true;
        closest=hit_distance;
        best.distance=float(hit_distance);
        float const inv=float(1.0/std::sqrt(norm2));
        for(int a=0;a<3;++a)best.normal[a]=surface.normal[a]*inv;
        best.surface_index=surface.surface_index;
    }
    return best;
}
} // namespace jkgm
