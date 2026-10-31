#pragma once
// Phase 1: build a *candidate* draw list from the immutable world VBO.
// These ranges have current GeoMode/MAT/cel and conservative camera-frustum
// filtering, but are NOT confirmed by JK's sector/portal visibility.
// Never use them to suppress the native world batch until the engine's actual
// per-frame SithSurface submissions can be associated with surface_index.
#include "world_cache.hpp"
#include "gpu_3do_frustum.hpp"
#include <array>
#include <cstdint>
#include <cstring>
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
    for(auto const &range:mesh.surfaces) {
        // SithSurface stride 0x5c. Read the live 0x10-0x3b metadata together
        // rather than performing three/four separately validated native reads.
        // The cache contains only immutable geometry; do not trust its original
        // MAT or cel once a COG has modified the surface.
        uint64_t const addr=uint64_t(surfaces_address)+
                            uint64_t(range.surface_index)*0x5cU;
        if(addr>0x7ff00000U) {++stats.unreadable;continue;}
        std::array<uint8_t,0x2c> raw{};
        if(!read(uintptr_t(addr)+0x10U,raw.data(),raw.size())) {
            ++stats.unreadable;continue;
        }
        uint32_t flags=0,geo=0,material=0,cel=0;
        std::memcpy(&flags,raw.data(),4);      // +0x10 surface flags
        std::memcpy(&geo,raw.data()+0x0c,4);   // +0x1c rdFace GeoMode
        std::memcpy(&material,raw.data()+0x24,4); // +0x34 rdMaterial
        std::memcpy(&cel,raw.data()+0x28,4);   // +0x38 wall cel
        if(uintptr_t(material)!=range.material)++stats.changed_material;
        if(range.cel>=0 && static_cast<int32_t>(cel)!=range.cel)
            ++stats.changed_cel;
        if(flags&0x600U) {++stats.sky;continue;}
        if(geo<2U) {++stats.hidden;continue;}
        // Phase-1 list is intentionally textured/opaque-*candidate* only.
        // GeoMode 2/3, solid adjoins (including grates), and non-UV faces
        // still require a material/alpha parity path and remain native.
        if(geo!=4U) {++stats.not_textured;continue;}
        if(!range.has_uv) {++stats.missing_uv;continue;}
        if(!material) {++stats.missing_material;continue;}
        if(range.solid_adjoin) {++stats.solid_adjoin;continue;}
        if(stats.frustum_available &&
           gpu_3do_batch_outside_frustum(*frustum,*view,
                                        range.bounds_min,range.bounds_max)) {
            ++stats.frustum_rejected;continue;
        }
        out.push_back({range.surface_index,range.first_index,range.count,
                       uintptr_t(material),static_cast<int32_t>(cel)});
        ++stats.candidates;
        stats.triangles+=range.count/3U;
    }
    return stats;
}
} // namespace jkgm
