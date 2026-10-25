#pragma once
// Spotlight shadow-caster geometry and perspective depth math.
// Pure CPU layer; does not create GL objects or touch the game executable.
// Reuses the existing per-level SithWorld snapshot/BVH from projected decals.
#include "spotlight.hpp"
#include "sprite_decal_world_cache.hpp"
#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <limits>
#include <vector>

namespace jkgm {

// One spotlight shadow texture initially, before adding per-light budgets.
constexpr unsigned spotlight_shadow_initial_resolution=256U;
constexpr unsigned spotlight_shadow_max_resolution=1024U;

struct spotlight_shadow_projection {
    decal_vec3 origin{},right{},up{},forward{};
    float width=1.0f,height=1.0f,range=1.0f;
    float near_distance=0.02f;
};
struct spotlight_shadow_clip_point {
    float x=0.0f,y=0.0f,z=0.0f,w=0.0f;
};
struct spotlight_shadow_surface_range {
    uint32_t first_index=0,count=0;
};
struct spotlight_shadow_static_mesh {
    // Indexed vertices, retaining each native polygon vertex just once.
    // The surfaces vector has EXACTLY the order of world.receivers, so a
    // receiver BVH query indexes the corresponding draw range directly.
    std::vector<decal_vec3> vertices;
    std::vector<uint32_t> indices;
    std::vector<spotlight_shadow_surface_range> surfaces;
};

inline bool make_spotlight_shadow_projection(
    spotlight_instance const &spot,spotlight_shadow_projection &out)
{
    if(!(std::isfinite(spot.width)&&std::isfinite(spot.height)&&
         std::isfinite(spot.range)) ||
       spot.width<=0.0f || spot.height<=0.0f || spot.range<=0.05f)
        return false;
    if(!decal_finite(spot.origin)||!decal_finite(spot.right)||
       !decal_finite(spot.up)||!decal_finite(spot.forward))
        return false;
    // Do not allow an invalid orientation to put every receiver in shadow.
    if(std::fabs(decal_dot(spot.forward,spot.forward)-1.0f)>0.02f ||
       std::fabs(decal_dot(spot.right,spot.right)-1.0f)>0.02f ||
       std::fabs(decal_dot(spot.up,spot.up)-1.0f)>0.02f ||
       std::fabs(decal_dot(spot.right,spot.up))>0.02f ||
       std::fabs(decal_dot(spot.right,spot.forward))>0.02f ||
       std::fabs(decal_dot(spot.up,spot.forward))>0.02f)
        return false;
    out.origin=spot.origin;
    out.right=spot.right;
    out.up=spot.up;
    out.forward=spot.forward;
    out.width=spot.width;
    out.height=spot.height;
    out.range=spot.range;
    out.near_distance=std::min(0.05f,std::max(0.005f,spot.range*0.005f));
    return true;
}

// Must match the existing COG MAT cookie projection exactly, but supplies
// standard OpenGL perspective Z [-w,+w] for the hardware depth buffer.
inline spotlight_shadow_clip_point spotlight_shadow_clip(
    spotlight_shadow_projection const &p,decal_vec3 world)
{
    auto const rel=decal_sub(world,p.origin);
    float const z=decal_dot(rel,p.forward);
    float const n=p.near_distance,f=p.range;
    return {
        2.0f*decal_dot(rel,p.right)*f/p.width,
        2.0f*decal_dot(rel,p.up)*f/p.height,
        ((f+n)/(f-n))*z-2.0f*f*n/(f-n),
        z
    };
}

// The normalized depth value sampled from GL_DEPTH_COMPONENT16.
// Match the clip/w divide and default 0..1 depth range; this is non-linear.
inline float spotlight_shadow_depth(spotlight_shadow_projection const &p,
                                    float forward_distance)
{
    float const n=p.near_distance,f=p.range;
    return f/(f-n)-f*n/((f-n)*forward_distance);
}

// Conservative broad-phase bounds of the entire finite light pyramid.
// The tip and four far corners are sufficient for an affine 3D AABB.
inline decal_volume_aabb spotlight_shadow_volume_bounds(
    spotlight_shadow_projection const &p)
{
    decal_volume_aabb bounds{};
    for(int a=0;a<3;++a) {
        float const center=p.origin[a]+p.forward[a]*p.range;
        float const corner_extent=
            std::fabs(p.right[a])*p.width*0.5f+
            std::fabs(p.up[a])*p.height*0.5f;
        bounds.low[a]=std::min(p.origin[a],center-corner_extent)-0.001f;
        bounds.high[a]=std::max(p.origin[a],center+corner_extent)+0.001f;
    }
    return bounds;
}

// Build once per LEVEL, not per shadow frame or projector movement.
// No engine reads are performed: it consumes the already-validated decal
// snapshot, retaining world geometry independently of the main camera.
inline bool build_spotlight_shadow_static_mesh(
    decal_world_geometry const &world,spotlight_shadow_static_mesh &out)
{
    out={};
    if(!world.valid_world)return false;
    uint64_t vertex_total=0,index_total=0;
    for(auto const &surface:world.receivers) {
        auto const n=surface.polygon.size();
        if(n<3 || n>128)return false;
        vertex_total+=n;
        index_total+=3*(n-2);
        if(vertex_total>500000U || index_total>1500000U)return false;
        for(auto const &v:surface.polygon)
            if(!decal_finite(v))return false;
    }
    out.vertices.reserve(static_cast<size_t>(vertex_total));
    out.indices.reserve(static_cast<size_t>(index_total));
    out.surfaces.reserve(world.receivers.size());
    for(auto const &surface:world.receivers) {
        uint32_t const base=static_cast<uint32_t>(out.vertices.size());
        uint32_t const offset=static_cast<uint32_t>(out.indices.size());
        // Impassable adjoins can carry alpha-tested grate/glass MATs.
        // Their material alpha isn't available in this untextured depth
        // pass, so exclude the WHOLE adjoin rather than cast an opaque
        // rectangle. This is conservative until texture-aware shadows.
        if(surface.solid_adjoin) {
            out.surfaces.push_back({offset,0U});
            continue;
        }
        for(auto const &v:surface.polygon)out.vertices.push_back(v);
        // JK level polygons are expected to be planar and convex. The
        // original decal receiver cache uses the same fan assumption.
        for(uint32_t i=1;i+1<surface.polygon.size();++i) {
            out.indices.push_back(base);
            out.indices.push_back(base+i);
            out.indices.push_back(base+i+1);
        }
        out.surfaces.push_back({
            offset,static_cast<uint32_t>(out.indices.size())-offset
        });
    }
    return true;
}

inline void query_spotlight_shadow_static_mesh(
    decal_world_geometry const &world,
    spotlight_shadow_static_mesh const &mesh,
    spotlight_shadow_projection const &projection,
    std::vector<spotlight_shadow_surface_range> &out)
{
    out.clear();
    if(!world.valid_world || mesh.surfaces.size()!=world.receivers.size())
        return;
    auto const box=spotlight_shadow_volume_bounds(projection);
    std::vector<uint32_t> selected;
    world.acceleration.query(world.receivers,box.low,box.high,selected);
    out.reserve(selected.size());
    for(uint32_t id:selected)
        if(mesh.surfaces[id].count>0U)out.push_back(mesh.surfaces[id]);
}
} // namespace jkgm
