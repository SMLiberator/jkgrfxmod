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

// Compact mode 2: immutable surface normal and live UV scroll stay per
// surface. The validated SithSector index selects a separate, live 32B
// sector palette. Neither native surface nor sector pointers enter GLSL.
struct world_cache_surface_sector_descriptor {
    std::array<float,4> normal_scroll_x{{0,0,1,0}};
    // scroll.y, sector-index (exact FP32 up to 65,536), padding, padding.
    std::array<float,4> scroll_y_sector{{0,0,0,0}};
};
static_assert(sizeof(world_cache_surface_sector_descriptor)==32,
              "Mode-2 surface descriptor must occupy two RGBA32F texels");

struct world_cache_sector_descriptor {
    std::array<float,4> tint{{0,0,0,0}};
    std::array<float,4> cmp{{1,1,1,0}};
};
static_assert(sizeof(world_cache_sector_descriptor)==32,
              "Mode-2 shared SithSector descriptor must occupy two RGBA32F texels");

// Supported JK SithSector table is contiguous, 0x98 bytes per entry.
// Fail closed if ownership is not one of the live world's sector entries.
// On COG sector reassignment, the new valid index is staged on next draw.
inline bool world_cache_sector_index(
    uintptr_t owner,uintptr_t table,uint32_t count,uint32_t &id)
{
    id=0U;
    if(owner<0x10000U || table<0x10000U || !count || count>65536U ||
       owner<table || table>=0x7ff00000U)return false;
    uint64_t const offset=uint64_t(owner)-uint64_t(table);
    if(offset%0x98U!=0U || offset/0x98U>=count)return false;
    id=static_cast<uint32_t>(offset/0x98U);
    return true;
}

inline void build_world_cache_sector_surface_descriptors(
    std::vector<world_cache_surface_descriptor> const &original,
    std::vector<world_cache_surface_sector_descriptor> &out)
{
    out.clear();
    out.reserve(original.size());
    for(auto const &d:original) {
        world_cache_surface_sector_descriptor next{};
        next.normal_scroll_x=d.normal_scroll_x;
        out.push_back(next);
    }
}

inline bool stage_world_cache_sector_surface_descriptor(
    world_cache_mesh const &mesh,
    std::vector<uint32_t> const &lookup,
    world_cache_draw_candidate const &face,
    uintptr_t owner,uintptr_t sector_table,uint32_t sector_count,
    std::vector<std::array<float,3>> const &tints,
    std::vector<std::array<float,3>> const &cmps,
    std::vector<std::array<float,2>> const &offsets,
    std::vector<world_cache_surface_sector_descriptor> &surfaces,
    std::vector<world_cache_sector_descriptor> &sectors,
    uint32_t *staged_sector=nullptr)
{
    if(face.surface_index>=lookup.size())return false;
    uint32_t const si=lookup[face.surface_index];
    if(si>=mesh.surfaces.size() || si>=surfaces.size())return false;
    auto const &range=mesh.surfaces[si];
    if(range.surface_index!=face.surface_index ||
       range.first_index!=face.first_index ||
       range.count!=face.count || range.count<3U ||
       range.count%3U!=0U ||
       uint64_t(range.first_index)+range.count>mesh.indices.size())return false;
    uint32_t sector=0U;
    if(!world_cache_sector_index(owner,sector_table,sector_count,sector) ||
       sector>=sectors.size())return false;
    size_t const base=mesh.indices[range.first_index];
    if(base>=tints.size() || base>=cmps.size() || base>=offsets.size())
        return false;
    auto &s=surfaces[si];
    s.normal_scroll_x[3]=offsets[base][0];
    s.scroll_y_sector={{offsets[base][1],float(sector),0.0f,0.0f}};
    auto &d=sectors[sector];
    d.tint={{tints[base][0],tints[base][1],tints[base][2],0.0f}};
    d.cmp={{cmps[base][0],cmps[base][1],cmps[base][2],0.0f}};
    if(staged_sector)*staged_sector=sector;
    return true;
}

// Same bounded dirty-span machinery used for per-surface descriptors, now
// in the native sector-ID domain. Multiple staged faces of one sector count
// only once, including faces from main and reflected camera worksets.
inline world_cache_stream_plan plan_world_cache_sector_descriptor_stream(
    size_t total_sector_count,std::vector<uint32_t> const &selected)
{
    world_cache_stream_plan plan{};
    plan.valid=true;
    std::vector<uint32_t> unique=selected;
    std::sort(unique.begin(),unique.end());
    unique.erase(std::unique(unique.begin(),unique.end()),unique.end());
    for(uint32_t id:unique) {
        if(id>=total_sector_count) {
            plan.valid=false;
            plan.ranges.clear();
            return plan;
        }
        if(!plan.ranges.empty() &&
           plan.ranges.back().first_vertex+plan.ranges.back().count==id)
            ++plan.ranges.back().count;
        else plan.ranges.push_back({id,1U});
    }
    plan.touched_vertices=unique.size();
    plan.partial=plan.ranges.size()<=12U &&
                 plan.touched_vertices<=total_sector_count/4U;
    return plan;
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
