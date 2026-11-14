#pragma once
// Thread-safe exchange between GL's completed async Thing-occlusion queries
// and the native rdModel3_Draw hook. No GL calls, pose work or GPU waits here.
// Hidden native Things must still be sent back as lightweight query candidates.
#include "gpu_thing_occlusion_math.hpp"
#include <atomic>
#include <chrono>
#include <cstdint>
#include <memory>
#include <mutex>
#include <unordered_map>
#include <utility>
#include <vector>

namespace jkgm {
struct native_occlusion_candidate {
    uint64_t owner=0; // SithThing address (high) and signature (low)
    uint32_t model=0; // current rdModel3 pointer, not a source 3DO filename
    gpu_thing_occlusion_bound bound{};
};
struct native_occlusion_result {
    uint64_t owner=0;
    uint32_t model=0;
    gpu_thing_occlusion_bound bound{};
    engine_camera camera{};
};
struct native_occlusion_snapshot {
    uintptr_t world=0;
    std::chrono::steady_clock::time_point published_at{};
    std::unordered_map<uint64_t,native_occlusion_result> hidden;
};
class native_occlusion_bridge {
    mutable std::mutex candidates_mutex_;
    std::unordered_map<uint64_t,native_occlusion_candidate> candidates_;
    using snapshot_ptr=std::shared_ptr<native_occlusion_snapshot const>;
    // C++20 offers atomic<shared_ptr>. The older free-function overloads are
    // deprecated there (MSVC STL4029), but still required for C++17 builds.
#if defined(__cpp_lib_atomic_shared_ptr) && __cpp_lib_atomic_shared_ptr >= 201711L
    std::atomic<snapshot_ptr> published_{};
    void store_snapshot(snapshot_ptr next) {
        published_.store(std::move(next),std::memory_order_release);
    }
    snapshot_ptr load_snapshot() const {
        return published_.load(std::memory_order_acquire);
    }
#else
    snapshot_ptr published_;
    void store_snapshot(snapshot_ptr next) {
        std::atomic_store_explicit(&published_,std::move(next),
                                   std::memory_order_release);
    }
    snapshot_ptr load_snapshot() const {
        return std::atomic_load_explicit(&published_,std::memory_order_acquire);
    }
#endif
public:
    void register_candidate(native_occlusion_candidate const &candidate) {
        if(!candidate.owner || !candidate.model ||
           !gpu_thing_occlusion_bound_valid(candidate.bound))return;
        std::lock_guard<std::mutex> lock(candidates_mutex_);
        candidates_[candidate.owner]=candidate;
    }
    std::vector<native_occlusion_candidate> take_candidates() {
        std::unordered_map<uint64_t,native_occlusion_candidate> found;
        {
            std::lock_guard<std::mutex> lock(candidates_mutex_);
            found.swap(candidates_);
        }
        std::vector<native_occlusion_candidate> out;
        out.reserve(found.size());
        for(auto const &entry:found)out.push_back(entry.second);
        return out;
    }
    void publish(uintptr_t world,
                 std::vector<native_occlusion_result> const &hidden) {
        auto next=std::make_shared<native_occlusion_snapshot>();
        next->world=world;
        next->published_at=std::chrono::steady_clock::now();
        next->hidden.reserve(hidden.size());
        for(auto const &entry:hidden)
            if(entry.owner && entry.model &&
               gpu_thing_occlusion_bound_valid(entry.bound))
                next->hidden.emplace(entry.owner,entry);
        std::shared_ptr<native_occlusion_snapshot const> immutable=next;
        store_snapshot(std::move(immutable));
    }
    // Cheap eligibility check before the native hook reads the live camera.
    // Validate the complete current bound/camera/world AGAIN before skipping.
    bool lookup(uint64_t owner,uint32_t model,
                gpu_thing_occlusion_bound const &bound,
                native_occlusion_result &out,
                uintptr_t &published_world) const {
        published_world=0;
        auto const snap=load_snapshot();
        if(!snap || !snap->world ||
           std::chrono::steady_clock::now()-snap->published_at>
               std::chrono::milliseconds(250))
            return false;
        auto it=snap->hidden.find(owner);
        if(it==snap->hidden.end() || it->second.model!=model ||
           !gpu_thing_occlusion_same_bound(bound,it->second.bound))
            return false;
        out=it->second;
        published_world=snap->world;
        return true;
    }
    void clear() {
        {
            std::lock_guard<std::mutex> lock(candidates_mutex_);
            candidates_.clear();
        }
        store_snapshot({});
    }
};
inline native_occlusion_bridge &get_native_occlusion_bridge() {
    static native_occlusion_bridge value;
    return value;
}
} // namespace jkgm
