#pragma once
// Share native MAT residency results across GPU-cached meshes in one frame.
// Positive results only: a previously missing cel can become resident after
// any native renderer call, even before a frame boundary.
#include <cstdint>
#include <unordered_set>

namespace jkgm {
class gpu_mat_ready_cache {
    uint64_t frame_=0,upload_generation_=0;
    std::unordered_set<uint64_t> ready_;
    static uint64_t key(uintptr_t mat,uint32_t cel) {
        // JKGM is 32-bit: native rdMaterial pointers are 32 bits.
        return (uint64_t(static_cast<uint32_t>(mat))<<32) | uint64_t(cel);
    }
    void sync(uint64_t frame,uint64_t upload_generation) {
        if(frame_!=frame || upload_generation_!=upload_generation) {
            ready_.clear();
            frame_=frame;
            upload_generation_=upload_generation;
        }
    }
public:
    bool contains(uint64_t frame,uint64_t upload_generation,
                  uintptr_t mat,uint32_t cel) {
        sync(frame,upload_generation);
        return ready_.find(key(mat,cel))!=ready_.end();
    }
    void mark_ready(uint64_t frame,uint64_t upload_generation,
                    uintptr_t mat,uint32_t cel) {
        sync(frame,upload_generation);
        ready_.insert(key(mat,cel));
    }
    void clear() {
        ready_.clear();
        frame_=0;
        upload_generation_=0;
    }
};
}
