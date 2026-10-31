#pragma once
// Phase 0 of native SithWorld visible-geometry caching.
// This is an immutable geometry candidate cache, NOT yet a replacement for
// JK's sector/adjoin visibility, dynamic world lighting or transparency.
// The native execute-buffer renderer remains authoritative until parity
// (including sky, portals, material-less faces, and HOM) is demonstrated.
#include "sprite_decal_world_cache.hpp"
#include <array>
#include <cmath>
#include <cstdint>
#include <limits>
#include <vector>

namespace jkgm {
struct world_cache_vertex {
    std::array<float,3> position{};
    std::array<float,2> uv{};
    std::array<float,3> normal{};
};
static_assert(sizeof(world_cache_vertex)==sizeof(float)*8,
              "World cache vertex layout must match GPU attributes");

struct world_cache_surface_range {
    uint32_t surface_index=0; // stable SithWorld index, NOT receiver index
    uint32_t first_index=0, count=0;
    uintptr_t material=0; // native rdMaterial, read-only
    int32_t cel=-1;
    bool solid_adjoin=false;
    bool has_uv=false;
};

struct world_cache_mesh {
    std::vector<world_cache_vertex> vertices;
    std::vector<uint32_t> indices;
    std::vector<world_cache_surface_range> surfaces;
    uint32_t uv_ready_surfaces=0,missing_uv_surfaces=0;
};

// Snapshot the static geometry only once per world topology. Surface state
// (geomode, material cel, animation, scroll, visibility) is intentionally
// NOT baked into the geometry; callers must query it live before rendering.
// Shadow-world receivers omit sky/passable portals and other special faces:
// never assume this mesh covers every native world polygon.
inline bool build_world_cache_mesh(decal_world_geometry const &world,
                                   world_cache_mesh &out)
{
    out={};
    if(!world.valid_world)return false;
    uint64_t verts=0,indices=0;
    for(auto const &surf:world.receivers) {
        size_t const n=surf.polygon.size();
        if(n<3U || n>128U)return false;
        verts+=n;
        indices+=3U*(n-2U);
        if(verts>500000U || indices>1500000U ||
           verts>std::numeric_limits<uint32_t>::max() ||
           indices>std::numeric_limits<uint32_t>::max())return false;
        for(auto const &p:surf.polygon)
            if(!decal_finite(p))return false;
    }
    out.vertices.reserve(static_cast<size_t>(verts));
    out.indices.reserve(static_cast<size_t>(indices));
    out.surfaces.reserve(world.receivers.size());
    for(auto const &surf:world.receivers) {
        uint32_t const base=static_cast<uint32_t>(out.vertices.size());
        uint32_t const begin=static_cast<uint32_t>(out.indices.size());
        bool valid_uv=surf.has_shadow_uv &&
                      surf.shadow_uv.size()==surf.polygon.size();
        if(valid_uv)for(auto const &uv:surf.shadow_uv)
            if(!std::isfinite(uv[0]) || !std::isfinite(uv[1])) {
                valid_uv=false;break;
            }
        // Preserve geometric winding and face normal; JK world geometry
        // has no object-space transform and fan triangulation is adequate
        // for the validated convex SithSurface polygon subset.
        if(!decal_finite(surf.normal))return false;
        for(size_t v=0;v<surf.polygon.size();++v) {
            world_cache_vertex vertex{};
            vertex.position=surf.polygon[v];
            vertex.normal=surf.normal;
            if(valid_uv)vertex.uv=surf.shadow_uv[v];
            out.vertices.push_back(vertex);
        }
        for(uint32_t t=1;t+1<surf.polygon.size();++t) {
            out.indices.push_back(base);
            out.indices.push_back(base+t);
            out.indices.push_back(base+t+1);
        }
        out.surfaces.push_back({
            surf.surface_index,begin,
            static_cast<uint32_t>(out.indices.size())-begin,
            surf.material,surf.cel,surf.solid_adjoin,valid_uv
        });
        if(valid_uv)++out.uv_ready_surfaces;
        else ++out.missing_uv_surfaces;
    }
    return true;
}

// Read-only, bounded live metadata probe. A per-level geometry buffer may
// remain immutable when a COG changes GeoMode/cel, but draw selection and
// material binding must use current native surface state, not the snapshot.
struct world_cache_live_state {
    uint32_t drawn_candidates=0,hidden=0,missing_uv=0;
    uint32_t changed_cel=0,changed_material=0;
    uint32_t unreadable=0;
};
inline world_cache_live_state inspect_world_cache_live_state(
    decal_native_read const &read,uintptr_t surfaces_address,
    world_cache_mesh const &mesh)
{
    world_cache_live_state stats{};
    if(!read || !decal_native_address_ok(surfaces_address)) {
        stats.unreadable=static_cast<uint32_t>(mesh.surfaces.size());
        return stats;
    }
    for(auto const &range:mesh.surfaces) {
        uint64_t const addr=uint64_t(surfaces_address)+
                            uint64_t(range.surface_index)*0x5cU;
        if(addr>0x7ff00000U) {++stats.unreadable;continue;}
        uint32_t geomode=0,material=0,cel=0;
        if(!read_decal_u32(read,uintptr_t(addr)+0x1cU,geomode) ||
           !read_decal_u32(read,uintptr_t(addr)+0x34U,material) ||
           !read_decal_u32(read,uintptr_t(addr)+0x38U,cel)) {
            ++stats.unreadable;continue;
        }
        if(geomode<2U) {++stats.hidden;continue;}
        if(!range.has_uv) {++stats.missing_uv;continue;}
        ++stats.drawn_candidates;
        if(uintptr_t(material)!=range.material)++stats.changed_material;
        if(range.cel>=0 && int32_t(cel)!=range.cel)++stats.changed_cel;
    }
    return stats;
}
} // namespace jkgm
