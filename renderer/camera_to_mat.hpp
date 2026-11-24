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
// COG CameraToMatEx mode bits. Full shading (0x0/0x1) temporarily
// falls back to simplified rendering until a full secondary pass exists.
constexpr uint32_t camera_to_mat_ortho=0x1U;
constexpr uint32_t camera_to_mat_simplified=0x2U;
constexpr uint32_t camera_to_mat_unlit=0x4U;
inline bool camera_to_mat_valid_mode(uint32_t mode) {
    return (mode&~0x7U)==0U &&
        (mode&(camera_to_mat_simplified|camera_to_mat_unlit))!=
        (camera_to_mat_simplified|camera_to_mat_unlit);
}
struct camera_to_mat_request {
    uint32_t mat=0;
    uint64_t owner=0; // native Thing address/signature
    uint32_t world=0;
    float fov=90.0f; // internal signed FOV: >0 perspective, <0 JKU height
    uint32_t mode=camera_to_mat_simplified;
    uint64_t generation=0;
};
class camera_to_mat_controls {
    mutable std::mutex mutex_;
    std::map<uint32_t,camera_to_mat_request> requests_;
    uint32_t world_=0;
    uint64_t generation_=0;
    bool queue(uint32_t mat,uint64_t owner,uint32_t world,float signed_fov,
               uint32_t mode) {
        if(mat>65535U || (mat&0x7fffU)>=16384U || !world ||
           !owner || uint32_t(owner)==0U || !camera_to_mat_valid_mode(mode) ||
           !std::isfinite(signed_fov) || signed_fov>=179.0f)
            return false;
        // Both verbs treat FOV=0 as a no-op without consuming a slot or
        // changing the pending/completed photograph.
        if(signed_fov==0.0f)return true;
        std::lock_guard<std::mutex> lock(mutex_);
        if(world_ && world_!=world)requests_.clear();
        world_=world;
        if(!requests_.count(mat) && requests_.size()>=max_targets)return false;
        requests_[mat]={mat,owner,world,signed_fov,mode,++generation_};
        return true;
    }
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
    // Legacy CameraToMat: signed FOV, always simplified shading.
    bool request(uint32_t mat,uint64_t owner,uint32_t world,float fov) {
        return queue(mat,owner,world,fov,camera_to_mat_simplified);
    }
    // CameraToMatEx: positive FOV/area and an explicit projection/mode mask.
    bool request_ex(uint32_t mat,uint64_t owner,uint32_t world,
                    float fov,uint32_t mode) {
        if(!camera_to_mat_valid_mode(mode) || !std::isfinite(fov) ||
           fov<0.0f ||
           (!(mode&camera_to_mat_ortho) && fov>=179.0f))
            return false;
        return queue(mat,owner,world,
            (mode&camera_to_mat_ortho)?-fov:fov,mode);
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
