#pragma once
// COG thread -> renderer one-shot capture requests. A completed photograph
// remains the MAT's albedo image without accessing its source Thing again.
// Authored material properties and the active native rdCamera never change.
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <map>
#include <mutex>
#include <vector>

namespace jkgm {
struct camera_to_mat_request {
    uint32_t mat=0;
    uint64_t owner=0; // native Thing address/signature
    uint32_t world=0;
    float fov=90.0f; // >0 perspective degrees, <0 orthographic height in JK units
    uint64_t generation=0;
};
class camera_to_mat_controls {
    mutable std::mutex mutex_;
    std::map<uint32_t,camera_to_mat_request> requests_;
    uint32_t world_=0;
    uint64_t generation_=0;
public:
    static constexpr size_t max_targets=4U;
    static bool needs_capture(bool ready,uint64_t completed_generation,
                              uint64_t requested_generation) {
        return !ready || completed_generation!=requested_generation;
    }
    uint64_t revision() const {
        std::lock_guard<std::mutex> lock(mutex_);
        return generation_;
    }
    bool has_requests() const {
        std::lock_guard<std::mutex> lock(mutex_);
        return !requests_.empty();
    }
    void prepare_world(uint32_t world) {
        if(!world)return;
        std::lock_guard<std::mutex> lock(mutex_);
        if(world_ && world_!=world) {
            requests_.clear();
            ++generation_;
        }
        world_=world;
    }
    bool request(uint32_t mat,uint64_t owner,uint32_t world,float fov) {
        if(mat>65535U || (mat&0x7fffU)>=16384U || !world ||
           !owner || uint32_t(owner)==0U ||
           !std::isfinite(fov) || fov>=179.0f)return false;
        // FOV 0 requests no new photograph and never consumes a capture slot.
        // Existing photographs (and any queued one-shot) remain unchanged.
        if(fov==0.0f)return true;
        std::lock_guard<std::mutex> lock(mutex_);
        if(world_ && world_!=world)requests_.clear();
        world_=world;
        if(!requests_.count(mat) && requests_.size()>=max_targets)return false;
        requests_[mat]={mat,owner,world,fov,++generation_};
        return true;
    }
    void erase(uint32_t mat) {
        std::lock_guard<std::mutex> lock(mutex_);
        if(requests_.erase(mat))++generation_;
    }
    std::vector<camera_to_mat_request> snapshot() const {
        std::lock_guard<std::mutex> lock(mutex_);
        std::vector<camera_to_mat_request> out;
        out.reserve(requests_.size());
        for(auto const &entry:requests_)out.push_back(entry.second);
        return out;
    }
    void clear() {
        std::lock_guard<std::mutex> lock(mutex_);
        requests_.clear();
        ++generation_;
        world_=0;
    }
};
inline camera_to_mat_controls &get_camera_to_mat_controls() {
    static camera_to_mat_controls controls;
    return controls;
}
} // namespace jkgm
