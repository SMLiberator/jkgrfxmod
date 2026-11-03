#pragma once
// GPU cache requests native MAT/cel uploads without calling std3D from a
// draw callback. The signed, deduplicated requests are serviced after JK's
// normal Thing tick, with a bounded budget and per-world invalidation.
#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <map>
#include <mutex>
#include <utility>
#include <vector>
namespace jkgm {
struct native_mat_warm_request {
    uintptr_t material=0;
    int32_t cel=-1;
};
class native_mat_warm_queue {
    using key=std::pair<uintptr_t,int32_t>;
    mutable std::mutex lock_;
    std::map<key,uint64_t> pending_;
    uintptr_t world_=0,materials_=0;
    uint64_t tick_=0;
public:
    void prepare_world(uintptr_t world,uintptr_t materials) {
        std::lock_guard<std::mutex> guard(lock_);
        if(world_!=world || materials_!=materials) {
            pending_.clear();
            world_=world;
            materials_=materials;
            tick_=0;
        }
    }
    void request(uintptr_t material,int32_t cel) {
        if(material<0x10000U || cel< -1 || cel>65535)return;
        std::lock_guard<std::mutex> guard(lock_);
        if(pending_.size()>=4096U)return;
        pending_.emplace(key{material,cel},0U);
    }
    std::vector<native_mat_warm_request> due(size_t budget) {
        std::lock_guard<std::mutex> guard(lock_);
        ++tick_;
        std::vector<native_mat_warm_request> result;
        result.reserve(std::min(budget,pending_.size()));
        for(auto &entry:pending_) {
            if(result.size()==budget)break;
            if(entry.second>tick_)continue;
            entry.second=tick_+30; // Throttle failed/incomplete loads.
            result.push_back({entry.first.first,entry.first.second});
        }
        return result;
    }
    void finish(native_mat_warm_request const &r,bool loaded) {
        if(!loaded)return;
        std::lock_guard<std::mutex> guard(lock_);
        pending_.erase(key{r.material,r.cel});
    }
    size_t size() const {
        std::lock_guard<std::mutex> guard(lock_);
        return pending_.size();
    }
};
inline native_mat_warm_queue &get_native_mat_warm_queue() {
    static native_mat_warm_queue queue;
    return queue;
}
} // namespace jkgm
