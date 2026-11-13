#include "renderer/gpu_mesh_cache_eviction.hpp"
#include <cassert>
#include <cstdint>
#include <memory>
#include <unordered_map>

// Standalone Windows CI test.  Exercise the same unordered_map interface as
// the renderer, without depending on JK native structures or a GL context.
struct entry {
    uint64_t last_used_frame=0;
    std::shared_ptr<int const> geometry;
};
using cache_type=std::unordered_map<void const *,entry>;

int main()
{
    int keys[12]{};
    cache_type cache;
    for(int i=0;i<8;++i) {
        entry e{};
        e.last_used_frame=i<4?10U:197U;
        e.geometry=std::make_shared<int const>(i+1);
        cache.emplace(&keys[i],std::move(e));
    }
    // A cache below its soft limit must not be touched.
    auto r=jkgm::trim_gpu_mesh_cache(cache,200U,9U,3U,2U);
    assert(r.evicted==0 && r.remaining==8 && !r.deferred);
    auto const held=cache.at(&keys[0]).geometry;
    // LRU reclaims the older half, retaining all recently referenced meshes.
    r=jkgm::trim_gpu_mesh_cache(cache,200U,8U,3U,2U);
    assert(r.evicted==3 && r.remaining==5 && !r.deferred);
    assert(cache.count(&keys[0])==0 && cache.count(&keys[3])==1);
    assert(cache.count(&keys[4])==1 && cache.count(&keys[7])==1);
    assert(*held==1); // current/pending draw can own the evicted geometry

    // Recently used entries must never be discarded just to meet a limit.
    cache.clear();
    for(int i=0;i<8;++i)
        cache.emplace(&keys[i],entry{199U,std::make_shared<int const>(i)});
    r=jkgm::trim_gpu_mesh_cache(cache,200U,8U,3U,2U);
    assert(r.evicted==0 && r.remaining==8 && r.deferred);
    // It becomes reclaimable after the protection window elapses.
    cache.at(&keys[7]).last_used_frame=204U;
    r=jkgm::trim_gpu_mesh_cache(cache,205U,8U,3U,2U);
    assert(r.evicted==3 && r.remaining==5 && !r.deferred);
    assert(cache.count(&keys[7])==1);

    // Churn may exceed the soft budget while everything is hot, but must
    // eventually converge by targeted eviction rather than a full clear.
    cache.clear();
    for(int i=0;i<12;++i)
        cache.emplace(&keys[i],entry{500U,std::make_shared<int const>(i)});
    r=jkgm::trim_gpu_mesh_cache(cache,500U,8U,3U,128U);
    assert(r.evicted==0 && r.deferred && cache.size()==12);
    cache.at(&keys[11]).last_used_frame=630U;
    r=jkgm::trim_gpu_mesh_cache(cache,631U,8U,3U,128U);
    assert(r.evicted==7 && r.remaining==5 && !r.deferred);
    assert(cache.count(&keys[11])==1);

    // Zero means no policy (for compatibility with diagnostic/test callers).
    auto const size=cache.size();
    r=jkgm::trim_gpu_mesh_cache(cache,2000U,0U);
    assert(r.evicted==0 && cache.size()==size);
}
