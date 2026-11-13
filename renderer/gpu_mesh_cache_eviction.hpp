#pragma once
// Shared, bounded-pressure reclamation of CPU 3DO geometry snapshots.
// This intentionally does NOT invalidate a draw instance's shared_ptr or
// reuse a GPU geometry ID. Recently seen meshes stay resident across both
// the primary and independent planar-reflection cameras.
#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <utility>
#include <vector>

namespace jkgm {
    constexpr std::size_t gpu_mesh_cache_soft_limit=8192U;
    constexpr std::size_t gpu_mesh_cache_reclaim=1024U;
    constexpr uint64_t gpu_mesh_cache_protected_frames=128U;

    struct gpu_mesh_cache_trim_result {
        std::size_t evicted=0;
        std::size_t remaining=0;
        bool deferred=false; // active/recent meshes exceeded the soft budget
    };

    template<class Cache>
    gpu_mesh_cache_trim_result trim_gpu_mesh_cache(
        Cache &cache,uint64_t frame,
        std::size_t limit=gpu_mesh_cache_soft_limit,
        std::size_t reclaim=gpu_mesh_cache_reclaim,
        uint64_t protect_frames=gpu_mesh_cache_protected_frames)
    {
        gpu_mesh_cache_trim_result result{};
        result.remaining=cache.size();
        if(limit==0 || cache.size()<limit)return result;
        // A soft budget: never discard geometry used by this frame, by a
        // recently completed GPU frame, or by the offscreen capture. If all
        // meshes are hot, defer reclamation rather than lose all snapshots.
        std::size_t const target=limit-(std::min)(reclaim,limit-1U);
        using key=typename Cache::key_type;
        std::vector<std::pair<uint64_t,key>> stale;
        stale.reserve(cache.size());
        for(auto const &item:cache) {
            uint64_t const last=item.second.last_used_frame;
            if(frame>=last && frame-last>protect_frames)
                stale.emplace_back(last,item.first);
        }
        std::sort(stale.begin(),stale.end(),
            [](std::pair<uint64_t,key> const &a,
               std::pair<uint64_t,key> const &b) {
                return a.first<b.first; // least recently used first
            });
        for(auto const &item:stale) {
            if(cache.size()<=target)break;
            result.evicted+=cache.erase(item.second);
        }
        result.remaining=cache.size();
        result.deferred=result.remaining>=limit;
        return result;
    }
}
