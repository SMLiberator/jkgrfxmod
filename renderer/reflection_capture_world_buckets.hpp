#pragma once
// Immutable secondary-capture world material groups. The renderer still
// evaluates independent portal/frustum/hemisphere visibility for each view.
#include <cstddef>
#include <cstdint>
#include <map>
#include <tuple>
#include <utility>
#include <vector>

namespace jkgm {
using reflection_capture_material_key=std::tuple<uintptr_t,int32_t,bool>;
struct reflection_capture_world_bucket {
    reflection_capture_material_key key{};
    std::vector<size_t> surfaces; // indices into the immutable world mesh
};
class reflection_capture_world_buckets {
    uintptr_t world_=0U;
    uint64_t revision_=0U;
    size_t surface_count_=0U;
    bool valid_=false;
    std::vector<reflection_capture_world_bucket> buckets_;
public:
    template<class SurfaceList>
    bool refresh(uintptr_t world,uint64_t revision,SurfaceList const &surfaces)
    {
        if(valid_ && world_==world && revision_==revision &&
           surface_count_==surfaces.size())return false;
        // Keep original map key sorting and original polygon order within
        // each material bucket, rather than altering alpha-mask draw order.
        std::map<reflection_capture_material_key,std::vector<size_t>> sorted;
        for(size_t i=0;i<surfaces.size();++i) {
            auto const &s=surfaces[i];
            if(s.count<3U || !s.material || s.passable_adjoin ||
               (s.surface_flags&0x600U)!=0U)continue;
            sorted[std::make_tuple(s.material,s.cel,!s.has_uv)].push_back(i);
        }
        buckets_.clear();
        buckets_.reserve(sorted.size());
        for(auto &entry:sorted)
            buckets_.push_back({entry.first,std::move(entry.second)});
        world_=world;revision_=revision;surface_count_=surfaces.size();
        valid_=true;
        return true;
    }
    void invalidate() {
        valid_=false;buckets_.clear();
        world_=0U;revision_=0U;surface_count_=0U;
    }
    std::vector<reflection_capture_world_bucket> const &buckets() const {
        return buckets_;
    }
};
} // namespace jkgm
