#pragma once
// Phase 1: read-only world draw staging. Full scan remains an independent
// parity baseline for sector-first staging; the native renderer is unchanged.
#include "world_cache.hpp"
#include "gpu_3do_frustum.hpp"
#include <array>
#include <cstdint>
#include <cstring>
#include <limits>
#include <vector>

namespace jkgm {
struct world_cache_draw_candidate {
    uint32_t surface_index=0;
    uint32_t first_index=0,count=0;
    uintptr_t material=0;
    int32_t cel=-1;
};

struct world_cache_frame_stats {
    uint32_t candidates=0,triangles=0;
    uint32_t frustum_rejected=0,hidden=0,not_textured=0;
    uint32_t missing_uv=0,solid_adjoin=0,sky=0,missing_material=0;
    uint32_t changed_material=0,changed_cel=0,unreadable=0;
    bool frustum_available=false;
};


inline bool build_world_cache_surface_lookup(
    world_cache_mesh const &mesh,uint32_t native_surface_count,
    std::vector<uint32_t> &lookup)
{
    lookup.clear();
    if(native_surface_count>250000U ||
       mesh.surfaces.size()>native_surface_count)return false;
    lookup.assign(native_surface_count,std::numeric_limits<uint32_t>::max());
    for(size_t n=0;n<mesh.surfaces.size();++n) {
        auto const &range=mesh.surfaces[n];
        if(range.surface_index>=native_surface_count ||
           lookup[range.surface_index]!=std::numeric_limits<uint32_t>::max() ||
           range.count==0U || range.count%3U!=0U ||
           uint64_t(range.first_index)+range.count>mesh.indices.size()) {
            lookup.clear();return false;
        }
        lookup[range.surface_index]=static_cast<uint32_t>(n);
    }
    return true;
}

// Shared eligibility logic for whole-world and sector-first staging. Always
// read live GeoMode/sky flags/material/cel; the VBO holds only geometry.
inline void append_world_cache_candidate(
    decal_native_read const &read,uintptr_t surfaces_address,
    world_cache_surface_range const &range,
    std::vector<world_cache_draw_candidate> &out,
    world_cache_frame_stats &stats,
    gpu_3do_native_frustum const *frustum,
    gpu_3do_matrix34 const *view)
{
    // SithSurface stride 0x5c. Read the live 0x10-0x3b metadata together
    // rather than performing three/four separately validated native reads.
    // The cache contains only immutable geometry; do not trust its original
    // MAT or cel once a COG has modified the surface.
    uint64_t const addr=uint64_t(surfaces_address)+
                        uint64_t(range.surface_index)*0x5cU;
    if(addr>0x7ff00000U) {++stats.unreadable;return;}
    std::array<uint8_t,0x2c> raw{};
    if(!read(uintptr_t(addr)+0x10U,raw.data(),raw.size())) {
        ++stats.unreadable;return;
    }
    uint32_t flags=0,geo=0,material=0,cel=0;
    std::memcpy(&flags,raw.data(),4);      // +0x10 surface flags
    std::memcpy(&geo,raw.data()+0x0c,4);   // +0x1c rdFace GeoMode
    std::memcpy(&material,raw.data()+0x24,4); // +0x34 rdMaterial
    std::memcpy(&cel,raw.data()+0x28,4);   // +0x38 wall cel
    if(uintptr_t(material)!=range.material)++stats.changed_material;
    if(range.cel>=0 && static_cast<int32_t>(cel)!=range.cel)
        ++stats.changed_cel;
    if(flags&0x600U) {++stats.sky;return;}
    if(geo<2U) {++stats.hidden;return;}
    // Phase-1 list is intentionally textured/opaque-*candidate* only.
    // GeoMode 2/3, solid adjoins (including grates), and non-UV faces
    // still require a material/alpha parity path and remain native.
    if(geo!=4U) {++stats.not_textured;return;}
    if(!range.has_uv) {++stats.missing_uv;return;}
    if(!material) {++stats.missing_material;return;}
    if(range.solid_adjoin) {++stats.solid_adjoin;return;}
    if(stats.frustum_available &&
       gpu_3do_batch_outside_frustum(*frustum,*view,
                                    range.bounds_min,range.bounds_max)) {
        ++stats.frustum_rejected;return;
    }
    out.push_back({range.surface_index,range.first_index,range.count,
                   uintptr_t(material),static_cast<int32_t>(cel)});
    ++stats.candidates;
    stats.triangles+=range.count/3U;

}
inline world_cache_frame_stats collect_world_cache_draw_candidates(
    decal_native_read const &read,
    uintptr_t surfaces_address,
    world_cache_mesh const &mesh,
    std::vector<world_cache_draw_candidate> &out,
    gpu_3do_native_frustum const *frustum=nullptr,
    gpu_3do_matrix34 const *view=nullptr)
{
    world_cache_frame_stats stats{};
    out.clear();
    if(!read || !decal_native_address_ok(surfaces_address)) {
        stats.unreadable=static_cast<uint32_t>(mesh.surfaces.size());
        return stats;
    }
    stats.frustum_available=frustum!=nullptr && view!=nullptr;
    out.reserve(mesh.surfaces.size());
    for(auto const &range:mesh.surfaces)
        append_world_cache_candidate(read,surfaces_address,range,out,
                                     stats,frustum,view);
    return stats;
}
} // namespace jkgm
