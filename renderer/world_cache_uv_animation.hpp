#pragma once
// Native JK owns sithSurface animation timing. Keep static geometry/UVs;
// stream only each face's current rdFace.texVertOffset into the GPU pass.
#include "world_cache_visibility.hpp"
#include <array>
#include <cstdint>
#include <limits>
#include <vector>

namespace jkgm {
inline bool write_world_cache_uv_animation(
    world_cache_mesh const &mesh,
    std::vector<uint32_t> const &lookup,
    world_cache_draw_candidate const &candidate,
    std::vector<std::array<float,2>> &offsets,
    char const **failure_reason=nullptr)
{
    auto const reject=[&](char const *reason) {
        if(failure_reason)*failure_reason=reason;
        return false;
    };
    if(failure_reason)*failure_reason=nullptr;
    if(candidate.surface_index>=lookup.size() ||
       offsets.size()!=mesh.vertices.size())return reject("uv_candidate_or_output_size_invalid");
    auto const index=lookup[candidate.surface_index];
    if(index==std::numeric_limits<uint32_t>::max() ||
       index>=mesh.surfaces.size())return reject("uv_surface_lookup_unavailable");
    auto const &range=mesh.surfaces[index];
    if(range.surface_index!=candidate.surface_index ||
       range.first_index!=candidate.first_index ||
       range.count!=candidate.count ||
       candidate.count<3U || candidate.count%3U!=0U ||
       uint64_t(candidate.first_index)+candidate.count>mesh.indices.size())
        return reject("uv_cached_surface_mapping_mismatch");
    // A fan of N vertices has 3*(N-2) indices. Unique world vertices
    // for this polygon occupy N contiguous entries starting at its first.
    size_t const begin=mesh.indices[candidate.first_index];
    size_t const count=size_t(candidate.count/3U)+2U;
    if(begin>offsets.size() || count>offsets.size()-begin)return reject("uv_cached_vertex_range_invalid");
    for(size_t i=begin;i<begin+count;++i)offsets[i]=candidate.uv_offset;
    return true;
}
} // namespace jkgm
