#pragma once
// Experimental world-cache descriptor format: original FP32 world positions
// and MAT UVs plus a stable per-surface ID. Immutable normals and mutable
// sector/CMP/SlideSurface state are shared across each surface's vertices.
#include "world_cache_streaming.hpp"
#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <vector>

namespace jkgm {
struct world_cache_descriptor_vertex {
    std::array<float,3> position{};
    std::array<float,2> uv{};
    uint32_t surface=0; // index in mesh.surfaces, not native surface ID
};
static_assert(sizeof(world_cache_descriptor_vertex)==24,
    "World descriptor vertex must remain exactly 24 bytes");
static_assert(offsetof(world_cache_descriptor_vertex,uv)==12 &&
              offsetof(world_cache_descriptor_vertex,surface)==20,
              "World descriptor vertex offsets changed");

struct world_cache_surface_descriptor {
    // Three RGBA32F texels: normal.xyz / scroll.x;
    // sector_tint.xyz / scroll.y; sector_cmp_tint.xyz / reserved.
    std::array<float,4> normal_scroll_x{{0,0,1,0}};
    std::array<float,4> tint_scroll_y{{0,0,0,0}};
    std::array<float,4> cmp_reserved{{1,1,1,0}};
};
static_assert(sizeof(world_cache_surface_descriptor)==48,
    "World descriptor must map exactly to 3 RGBA32F texels");

inline bool build_world_cache_descriptors(
    world_cache_mesh const &mesh,
    std::vector<world_cache_descriptor_vertex> &vertices,
    std::vector<world_cache_surface_descriptor> &descriptors)
{
    vertices.clear();
    descriptors.clear();
    if(mesh.surfaces.empty() || mesh.vertices.empty() ||
       mesh.surfaces.size()>std::numeric_limits<uint32_t>::max())
        return false;
    vertices.reserve(mesh.vertices.size());
    descriptors.reserve(mesh.surfaces.size());
    size_t next=0;
    for(size_t id=0;id<mesh.surfaces.size();++id) {
        auto const &range=mesh.surfaces[id];
        if(range.count<3U || range.count%3U!=0U ||
           uint64_t(range.first_index)+range.count>mesh.indices.size())goto reject;
        size_t const base=mesh.indices[range.first_index];
        size_t const count=size_t(range.count)/3U+2U;
        if(base!=next || count>mesh.vertices.size()-next)goto reject;
        auto const normal=mesh.vertices[base].normal;
        for(float value:normal)
            if(!std::isfinite(value))goto reject;
        world_cache_surface_descriptor d{};
        d.normal_scroll_x[0]=normal[0];
        d.normal_scroll_x[1]=normal[1];
        d.normal_scroll_x[2]=normal[2];
        descriptors.push_back(d);
        for(size_t i=base;i<base+count;++i) {
            auto const &v=mesh.vertices[i];
            if(v.normal!=normal)goto reject;
            vertices.push_back({v.position,v.uv,static_cast<uint32_t>(id)});
        }
        next+=count;
    }
    if(next!=mesh.vertices.size())goto reject;
    return true;
reject:
    vertices.clear();
    descriptors.clear();
    return false;
}

inline bool stage_world_cache_surface_descriptor(
    world_cache_mesh const &mesh,
    std::vector<uint32_t> const &lookup,
    world_cache_draw_candidate const &face,
    std::vector<std::array<float,3>> const &sector_tint,
    std::vector<std::array<float,3>> const &sector_cmp_tint,
    std::vector<std::array<float,2>> const &uv_offset,
    std::vector<world_cache_surface_descriptor> &descriptors)
{
    if(face.surface_index>=lookup.size())return false;
    uint32_t const id=lookup[face.surface_index];
    if(id>=mesh.surfaces.size() || id>=descriptors.size())return false;
    auto const &range=mesh.surfaces[id];
    if(range.surface_index!=face.surface_index ||
       range.first_index!=face.first_index || range.count!=face.count ||
       face.count<3U || face.count%3U!=0U ||
       uint64_t(face.first_index)+face.count>mesh.indices.size())
        return false;
    size_t const base=mesh.indices[face.first_index];
    if(base>=sector_tint.size() || base>=sector_cmp_tint.size() ||
       base>=uv_offset.size())return false;
    auto &d=descriptors[id];
    d.normal_scroll_x[3]=uv_offset[base][0];
    d.tint_scroll_y={{sector_tint[base][0],sector_tint[base][1],
                      sector_tint[base][2],uv_offset[base][1]}};
    d.cmp_reserved={{sector_cmp_tint[base][0],sector_cmp_tint[base][1],
                     sector_cmp_tint[base][2],0.0f}};
    return true;
}

// Pass the currently staged main+reflected faces to the same exact dirty
// upload planner used for VBO streams, but in unique surface-index space.
inline world_cache_stream_plan plan_world_cache_descriptor_stream(
    world_cache_mesh const &mesh,
    std::vector<uint32_t> const &lookup,
    std::vector<world_cache_draw_candidate> const &candidates)
{
    world_cache_stream_plan plan{};
    plan.valid=true;
    std::vector<size_t> ids;
    ids.reserve(candidates.size());
    for(auto const &face:candidates) {
        if(face.surface_index>=lookup.size()) {
            plan.valid=false;return plan;
        }
        uint32_t const id=lookup[face.surface_index];
        if(id>=mesh.surfaces.size()) {
            plan.valid=false;return plan;
        }
        auto const &s=mesh.surfaces[id];
        if(s.surface_index!=face.surface_index ||
           s.first_index!=face.first_index || s.count!=face.count) {
            plan.valid=false;return plan;
        }
        ids.push_back(id);
    }
    std::sort(ids.begin(),ids.end());
    ids.erase(std::unique(ids.begin(),ids.end()),ids.end());
    for(size_t id:ids) {
        if(!plan.ranges.empty() &&
           plan.ranges.back().first_vertex+plan.ranges.back().count==id)
            ++plan.ranges.back().count;
        else plan.ranges.push_back({id,1U});
    }
    plan.touched_vertices=ids.size();
    plan.partial=plan.ranges.size()<=12U &&
                 plan.touched_vertices<=mesh.surfaces.size()/4U;
    return plan;
}
} // namespace jkgm
