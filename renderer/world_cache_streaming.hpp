#pragma once
// Convert portal-visible cached world faces into contiguous vertex ranges for
// live lighting/tint/UV streaming. The immutable geometry stays on the GPU.
// Keep a conservative full-upload fallback: too many glBufferSubData calls
// can outweigh saved bandwidth, particularly through Wine's GL thunk layer.
#include "world_cache_visibility.hpp"
#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <utility>
#include <vector>

namespace jkgm {
struct world_cache_stream_range {
    size_t first_vertex=0;
    size_t count=0;
};

struct world_cache_stream_plan {
    std::vector<world_cache_stream_range> ranges;
    size_t touched_vertices=0;
    bool valid=false;
    bool partial=false;
};

inline world_cache_stream_plan plan_world_cache_vertex_stream(
    world_cache_mesh const &mesh,
    std::vector<world_cache_draw_candidate> const &candidates)
{
    world_cache_stream_plan plan{};
    plan.valid=true;
    size_t const total=mesh.vertices.size();
    plan.ranges.reserve(candidates.size());
    for(auto const &face:candidates) {
        // The world cache stores each fan's unique vertices contiguously.
        // Do not issue GPU subrange writes from an invalid live candidate.
        if(face.count<3U || face.count%3U!=0U ||
           uint64_t(face.first_index)+face.count>mesh.indices.size()) {
            plan.valid=false;
            plan.ranges.clear();
            return plan;
        }
        size_t const begin=mesh.indices[face.first_index];
        size_t const count=size_t(face.count)/3U+2U;
        if(begin>total || count>total-begin) {
            plan.valid=false;
            plan.ranges.clear();
            return plan;
        }
        plan.ranges.push_back({begin,count});
    }
    std::sort(plan.ranges.begin(),plan.ranges.end(),
        [](world_cache_stream_range const &a,
           world_cache_stream_range const &b) {
            return a.first_vertex<b.first_vertex;
        });
    std::vector<world_cache_stream_range> merged;
    merged.reserve(plan.ranges.size());
    for(auto const &range:plan.ranges) {
        if(!merged.empty() &&
           range.first_vertex<=merged.back().first_vertex+merged.back().count) {
            auto &back=merged.back();
            size_t const end=std::max(
                back.first_vertex+back.count,range.first_vertex+range.count);
            back.count=end-back.first_vertex;
        } else merged.push_back(range);
    }
    plan.ranges=std::move(merged);
    for(auto const &range:plan.ranges)
        plan.touched_vertices+=range.count;

    // At most 12 runs * 5 live buffers; require at least 75% fewer bytes.
    // Fragmented visibility retains glBufferData's orphaning-friendly path.
    plan.partial=!plan.ranges.empty() &&
                 plan.ranges.size()<=12U &&
                 plan.touched_vertices<=total/4U;
    return plan;
}
} // namespace jkgm
