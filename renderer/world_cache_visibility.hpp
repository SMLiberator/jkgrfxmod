#pragma once
// Read-only world candidate staging from native SithWorld sectors/adjoins.
// Full scans remain an independent visibility parity baseline; the separate
// GPU-first opaque draw and native-batch isolation live in renderer.cpp.
#include "world_cache.hpp"
#include "gpu_3do_frustum.hpp"
#include <array>
#include <cmath>
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
    uint32_t face_type=0; // live native rdFace.type at surface +0x18
    bool solid_adjoin=false;
    std::array<float,2> uv_offset{}; // live rdFace.texVertOffset in MAT pixels
    uint32_t geometry_mode=4; // live 3=palette-indexed solid, 4=textured
    bool passable_adjoin=false; // movement-through flag 0x2, not alpha
};

struct world_cache_frame_stats {
    uint32_t candidates=0,triangles=0;
    uint32_t frustum_rejected=0,hidden=0,not_textured=0;
    uint32_t missing_uv=0,solid_adjoin=0,passable_adjoin=0,sky=0,missing_material=0;
    uint32_t translucent_face=0,special_candidates=0,solid_color=0;
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
    gpu_3do_matrix34 const *view,
    std::vector<world_cache_draw_candidate> *special=nullptr)
{
    // SithSurface stride 0x5c. Read live adjoin pointer at +0x0c along
    // with +0x10..0x43 face metadata in one validated native read.
    // The cache contains only immutable geometry; do not trust its original
    // MAT or cel once a COG has modified the surface.
    uint64_t const addr=uint64_t(surfaces_address)+
                        uint64_t(range.surface_index)*0x5cU;
    if(addr>0x7ff00000U) {++stats.unreadable;return;}
    std::array<uint8_t,0x38> raw{};
    if(!read(uintptr_t(addr)+0x0cU,raw.data(),raw.size())) {
        ++stats.unreadable;return;
    }
    uint32_t adjoin=0,flags=0,face_type=0,geo=0,material=0,cel=0;
    std::memcpy(&adjoin,raw.data(),4);      // +0x0c SithSurface.adjoin
    std::memcpy(&flags,raw.data()+4,4);     // +0x10 SithSurface flags
    std::memcpy(&face_type,raw.data()+0x0c,4); // +0x18 rdFace.type
    std::memcpy(&geo,raw.data()+0x10,4);    // +0x1c rdFace GeoMode
    std::memcpy(&material,raw.data()+0x28,4); // +0x34 rdMaterial
    std::memcpy(&cel,raw.data()+0x2c,4);    // +0x38 wall cel
    std::array<float,2> uv_offset{};
    std::memcpy(uv_offset.data(),raw.data()+0x30,8); // +0x3c native UV offset
    // Mode 3 has no UV array; an unused native offset may be stale or
    // nonfinite. Never reject its solid MAT color on texture-only data.
    if(geo==3U)uv_offset={{0.0f,0.0f}};
    else if(!std::isfinite(uv_offset[0]) ||
            !std::isfinite(uv_offset[1])) {
        ++stats.unreadable;return;
    }
    if(uintptr_t(material)!=range.material)++stats.changed_material;
    if(range.cel>=0 && static_cast<int32_t>(cel)!=range.cel)
        ++stats.changed_cel;
    if(flags&0x600U) {++stats.sky;return;}
    if(geo<2U) {++stats.hidden;return;}
    // Modes 0..2 do not draw filled polygons in hardware. Solid GeoMode
    // 3 takes its RGB from the native MAT/CMP, not from texture UVs.
    if(geo!=3U && geo!=4U) {++stats.not_textured;return;}
    if(geo==4U && !range.has_uv) {++stats.missing_uv;return;}
    if(!material) {++stats.missing_material;return;}
    // Passability belongs to the ADJOIN, not to rdFace.type. Sample the
    // current flags: COG can toggle an adjoin without rebuilding the VBO.
    // Invalid pointers/reads must never produce a phantom opaque wall.
    auto const adjoin_status=classify_decal_adjoin(read,adjoin);
    if(adjoin_status==decal_adjoin_status::invalid) {
        ++stats.unreadable;return;
    }
    bool const solid_adjoin=adjoin!=0U &&
        adjoin_status==decal_adjoin_status::solid;
    bool const passable_adjoin=
        adjoin_status==decal_adjoin_status::passable;
    // JK's native world path recognizes rdFace.type 0x2 as translucent.
    // Do not borrow 0x20 Z-write semantics from other Sith-engine variants.
    // The meaning of 0x4 in JK is not established, so do not infer it.
    bool const translucent=(face_type&0x2U)!=0U;
    if(solid_adjoin)++stats.solid_adjoin;
    if(passable_adjoin)++stats.passable_adjoin;
    if(translucent)++stats.translucent_face;
    if(solid_adjoin || translucent) {
        ++stats.special_candidates;
        if(special && (!stats.frustum_available ||
            !gpu_3do_batch_outside_frustum(*frustum,*view,
                                           range.bounds_min,range.bounds_max)))
            special->push_back({range.surface_index,range.first_index,
                range.count,uintptr_t(material),static_cast<int32_t>(cel),
                face_type,solid_adjoin,uv_offset,geo,passable_adjoin});
        return;
    }
    if(stats.frustum_available &&
       gpu_3do_batch_outside_frustum(*frustum,*view,
                                    range.bounds_min,range.bounds_max)) {
        ++stats.frustum_rejected;return;
    }
    out.push_back({range.surface_index,range.first_index,range.count,
                   uintptr_t(material),static_cast<int32_t>(cel),
                   face_type,false,uv_offset,geo,passable_adjoin});
    ++stats.candidates;
    if(geo==3U)++stats.solid_color;
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
