#pragma once
// Source: OpenJKDF2 sithRender_BuildVisibleSectorList/RenderSectors.
// The portal-clipped list contains sectors, each with a contiguous native
// SithSurface range. In the supplied JKGM binary the sector-list producer
// is at 0x4c6850; it writes [eax*4+0x6bba88] and increments [0x831938].
// The patched limit is 0x1b58 (7000), not the original 128.
// Do not use a failed or even a successful probe to suppress native rendering
// until runtime addresses and actual per-surface draw parity are established.
#include "world_cache_visibility.hpp"
#include <algorithm>
#include <cstdint>
#include <cstring>
#include <vector>

namespace jkgm {
// Verified from jkgm(5).exe, not OpenJKDF2's old symbol locations.
constexpr uintptr_t world_cache_native_visible_count=0x00831938U;
constexpr uintptr_t world_cache_native_visible_list=0x006BBA88U;
constexpr uint32_t world_cache_native_visible_capacity=0x1B58U;

struct world_cache_sector_span {
    uint32_t first_surface=0,surface_count=0;
};
struct world_cache_portal_stats {
    bool valid=false;
    char const *reason="unavailable";
    uint32_t sector_count=0,visible_sector_count=0;
    uint32_t member_surfaces=0,matched_candidates=0,rejected_candidates=0;
};

inline world_cache_portal_stats inspect_world_cache_portal_membership(
    decal_native_read const &read,uintptr_t native_world,
    decal_world_geometry const &world,
    std::vector<world_cache_draw_candidate> const &staged,
    uintptr_t visible_count_address,uintptr_t visible_list_address,
    std::vector<uint8_t> &membership,
    std::vector<world_cache_sector_span> *validated_spans=nullptr)
{
    world_cache_portal_stats stats{};
    membership.clear();
    if(validated_spans)validated_spans->clear();
    if(!read || !world.valid_world || world.world!=native_world ||
       !decal_native_address_ok(native_world) ||
       !decal_native_address_ok(world.surfaces_addr) ||
       !decal_native_address_ok(visible_count_address) ||
       !decal_native_address_ok(visible_list_address)) {
        stats.reason="invalid_source";return stats;
    }

    uint32_t sector_count=0,sector_table=0,visible_count=0;
    // SithWorld numSectors/sectors offsets (+0x4c/+0x50) match the
    // verified JKGM native world_header. Sector list locations below
    // are probed, not assumed to be correct for patched JKGM.
    if(!read_decal_u32(read,native_world+0x4cU,sector_count) ||
       !read_decal_u32(read,native_world+0x50U,sector_table) ||
       !read_decal_u32(read,visible_count_address,visible_count)) {
        stats.reason="unreadable_header";return stats;
    }
    stats.sector_count=sector_count;
    stats.visible_sector_count=visible_count;
    // The supplied JKGM image bounds its array to 0x1b58 entries.
    if(!sector_count || sector_count>65536U ||
       !visible_count || visible_count>world_cache_native_visible_capacity ||
       visible_count>sector_count ||
       !decal_native_address_ok(sector_table)) {
        stats.reason="count_or_table_out_of_range";return stats;
    }
    // Original 32-bit SithSector layout: stride 0x98, contiguous array.
    // Check each pointer against the world table before dereferencing it.
    uint64_t const table_start=sector_table;
    uint64_t const table_end=table_start+uint64_t(sector_count)*0x98U;
    uint64_t const surface_start=world.surfaces_addr;
    uint64_t const surface_end=
        surface_start+uint64_t(world.surface_count)*0x5cU;
    if(table_end>0x7ff00000ULL || surface_end>0x7ff00000ULL) {
        stats.reason="world_table_overflow";return stats;
    }

    std::vector<uint32_t> visible(visible_count);
    if(!read(visible_list_address,visible.data(),
             visible.size()*sizeof(uint32_t))) {
        stats.reason="unreadable_list";return stats;
    }
    membership.assign(world.surface_count,0);
    for(uint32_t const sector_ptr:visible) {
        if(uint64_t(sector_ptr)<table_start ||
           uint64_t(sector_ptr)>=table_end ||
           (uint64_t(sector_ptr)-table_start)%0x98U!=0U) {
            stats.reason="sector_pointer_not_in_world";break;
        }
        // Adjacent numSurfaces/surfaces pointers: one validated read.
        uint32_t surface_fields[2]{};
        if(!read(uintptr_t(sector_ptr)+0x24U,surface_fields,
                 sizeof(surface_fields))) {
            stats.reason="unreadable_sector";break;
        }
        uint32_t const num_surfaces=surface_fields[0];
        uint32_t const surfaces_ptr=surface_fields[1];
        if(!num_surfaces)continue;
        if(uint64_t(surfaces_ptr)<surface_start ||
           uint64_t(surfaces_ptr)>=surface_end ||
           (uint64_t(surfaces_ptr)-surface_start)%0x5cU!=0U) {
            stats.reason="sector_surface_span_invalid";break;
        }
        uint64_t const first=(uint64_t(surfaces_ptr)-surface_start)/0x5cU;
        if(uint64_t(num_surfaces)>world.surface_count-first) {
            stats.reason="sector_surface_count_invalid";break;
        }
        if(validated_spans)validated_spans->push_back({
            static_cast<uint32_t>(first),num_surfaces
        });
        for(uint32_t j=0;j<num_surfaces;++j) {
            auto &bit=membership[size_t(first)+j];
            if(!bit){bit=1;++stats.member_surfaces;}
        }
    }
    if(std::strcmp(stats.reason,"unavailable")!=0) {
        // Every pointer must validate or no result can be used.
        membership.clear();
        if(validated_spans)validated_spans->clear();
        return stats;
    }
    stats.valid=true;
    stats.reason="ok";
    for(auto const &candidate:staged) {
        if(candidate.surface_index<membership.size() &&
           membership[candidate.surface_index])++stats.matched_candidates;
        else ++stats.rejected_candidates;
    }
    return stats;
}


// Stage *only* faces in JK's validated visible-sector spans.
// Generation stamps avoid duplicate faces when sectors repeat, with no
// O(world surface count) clear each frame. No draw calls are made here.
struct world_cache_sector_stage_stats {
    world_cache_frame_stats faces{};
    uint32_t unique_surfaces=0,cached_surfaces=0,uncached_surfaces=0;
    uint32_t repeated_surfaces=0;
    bool valid=false;
};
inline world_cache_sector_stage_stats collect_world_cache_sector_candidates(
    decal_native_read const &read,uintptr_t surfaces_address,
    world_cache_mesh const &mesh,
    std::vector<uint32_t> const &surface_lookup,
    std::vector<world_cache_sector_span> const &validated_spans,
    std::vector<uint32_t> &seen_epochs,uint32_t &epoch,
    std::vector<world_cache_draw_candidate> &out,
    gpu_3do_native_frustum const *frustum=nullptr,
    gpu_3do_matrix34 const *view=nullptr)
{
    world_cache_sector_stage_stats stats{};
    out.clear();
    if(!read || !decal_native_address_ok(surfaces_address) ||
       seen_epochs.size()!=surface_lookup.size())return stats;
    for(auto const &span:validated_spans)
        if(uint64_t(span.first_surface)+span.surface_count>
           surface_lookup.size())return stats;
    if(++epoch==0U) {
        std::fill(seen_epochs.begin(),seen_epochs.end(),0U);
        epoch=1U;
    }
    stats.valid=true;
    stats.faces.frustum_available=frustum!=nullptr && view!=nullptr;
    uint32_t const missing=std::numeric_limits<uint32_t>::max();
    for(auto const &span:validated_spans) {
        for(uint32_t j=0;j<span.surface_count;++j) {
            uint32_t const surface=span.first_surface+j;
            if(seen_epochs[surface]==epoch) {
                ++stats.repeated_surfaces;continue;
            }
            seen_epochs[surface]=epoch;
            ++stats.unique_surfaces;
            uint32_t const range_index=surface_lookup[surface];
            if(range_index==missing) {++stats.uncached_surfaces;continue;}
            if(range_index>=mesh.surfaces.size() ||
               mesh.surfaces[range_index].surface_index!=surface) {
                out.clear();stats.valid=false;return stats;
            }
            ++stats.cached_surfaces;
            append_world_cache_candidate(
                read,surfaces_address,mesh.surfaces[range_index],
                out,stats.faces,frustum,view);
        }
    }
    return stats;
}

// Compare contents, not iteration order: stable surface/index range and
// live material/cel must match the full-world scan exactly.
struct world_cache_stage_parity {
    bool exact=false;
    uint32_t matched=0,only_full_scan=0,only_sector_stage=0;
    uint32_t changed_metadata=0;
};
inline world_cache_stage_parity compare_world_cache_candidate_sets(
    std::vector<world_cache_draw_candidate> const &full_scan,
    std::vector<uint8_t> const &membership,
    std::vector<world_cache_draw_candidate> const &sector_stage)
{
    std::vector<world_cache_draw_candidate> expected,actual=sector_stage;
    expected.reserve(full_scan.size());
    for(auto const &c:full_scan)
        if(c.surface_index<membership.size() && membership[c.surface_index])
            expected.push_back(c);
    auto less=[](world_cache_draw_candidate const &a,
                 world_cache_draw_candidate const &b) {
        return a.surface_index<b.surface_index;
    };
    std::sort(expected.begin(),expected.end(),less);
    std::sort(actual.begin(),actual.end(),less);
    world_cache_stage_parity r{};
    size_t i=0,j=0;
    while(i<expected.size() || j<actual.size()) {
        if(j==actual.size() || (i<expected.size() &&
           expected[i].surface_index<actual[j].surface_index)) {
            ++r.only_full_scan;++i;
        } else if(i==expected.size() ||
                  actual[j].surface_index<expected[i].surface_index) {
            ++r.only_sector_stage;++j;
        } else {
            auto const &a=expected[i++],&b=actual[j++];
            if(a.first_index!=b.first_index || a.count!=b.count ||
               a.material!=b.material || a.cel!=b.cel)
                ++r.changed_metadata;
            else ++r.matched;
        }
    }
    r.exact=!r.only_full_scan && !r.only_sector_stage &&
            !r.changed_metadata;
    return r;
}
} // namespace jkgm
