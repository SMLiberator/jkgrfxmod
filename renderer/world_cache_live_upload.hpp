#pragma once
// Exact per-attribute dirty planning for the persistent cached-world VBOs.
// Scan only vertex spans staged for this frame. Never let a partial upload
// discard previously uploaded hidden surfaces, which can become visible later.
#include "world_cache_streaming.hpp"
#include <algorithm>
#include <cstddef>
#include <cstring>
#include <type_traits>
#include <vector>

namespace jkgm {
struct world_cache_live_upload_plan {
    std::vector<world_cache_stream_range> ranges;
    size_t uploaded_vertices=0;
    bool full=false;
    bool empty() const { return !full && ranges.empty(); }
};

// Group changed vertices into bounded 64-vertex chunks, not one GL call
// for each changed corner. Use the existing <=12 calls / <=25% bandwidth
// policy to avoid expensive fragmented sub-updates on Wine/OpenGL.
template<class T>
world_cache_live_upload_plan plan_world_cache_live_upload(
    std::vector<T> const &live,std::vector<T> const &uploaded,
    world_cache_stream_plan const &visible)
{
    static_assert(std::is_trivially_copyable<T>::value,
                  "Live GPU attribute must support bytewise comparison");
    world_cache_live_upload_plan plan{};
    size_t const total=live.size();
    auto force_full=[&] {
        plan.full=true;
        plan.ranges.clear();
        plan.uploaded_vertices=total;
        return plan;
    };
    // If the legacy visibility plan was already a cheap partial update,
    // never replace it with a costly whole-buffer orphaning upload merely
    // because changed 64-vertex blocks are unusually fragmented.
    auto fallback=[&] {
        if(visible.valid && visible.partial) {
            plan.full=false;
            plan.ranges=visible.ranges;
            plan.uploaded_vertices=visible.touched_vertices;
            return plan;
        }
        return force_full();
    };
    // An invalid candidate plan or a replaced/mismatched shadow requires
    // complete resynchronization, not a potentially stale skipped update.
    if(!visible.valid || uploaded.size()!=total)return force_full();
    constexpr size_t block_vertices=64U;
    for(auto const &span:visible.ranges) {
        if(span.first_vertex>total ||
           span.count>total-span.first_vertex)
            return force_full();
        if(span.count==0U)continue;
        size_t const end=span.first_vertex+span.count;
        // Fast path: most sectors have unchanged tints and static UV offsets.
        if(std::memcmp(live.data()+span.first_vertex,
                       uploaded.data()+span.first_vertex,
                       span.count*sizeof(T))==0)
            continue;
        for(size_t first=span.first_vertex;first<end;) {
            size_t const count=std::min(block_vertices,end-first);
            if(std::memcmp(live.data()+first,uploaded.data()+first,
                           count*sizeof(T))!=0) {
                if(!plan.ranges.empty() &&
                   plan.ranges.back().first_vertex+
                       plan.ranges.back().count==first)
                    plan.ranges.back().count+=count;
                else
                    plan.ranges.push_back({first,count});
                plan.uploaded_vertices+=count;
                if(plan.ranges.size()>12U ||
                   plan.uploaded_vertices>total/4U)
                    return fallback();
            }
            first+=count;
        }
    }
    return plan;
}

// Apply only after the matching GL buffer operation. The shadow represents
// actual submitted contents, not merely the most recent CPU staging values.
template<class T>
void record_world_cache_live_upload(
    std::vector<T> const &live,std::vector<T> &uploaded,
    world_cache_live_upload_plan const &plan)
{
    if(plan.full) {
        uploaded=live;
        return;
    }
    for(auto const &range:plan.ranges)
        std::copy_n(live.begin()+range.first_vertex,range.count,
                    uploaded.begin()+range.first_vertex);
}
} // namespace jkgm
