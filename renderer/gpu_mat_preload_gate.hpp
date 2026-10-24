#pragma once
// One-shot candidate gate for the SHA-verified JK material cache uploader.
// Used only from active native mesh processing, never compositor frames.
#include <cstdint>
#include <cstddef>
#include <unordered_set>

namespace jkgm {
class gpu_mat_preload_gate {
    uint32_t world_=0;
    std::unordered_set<uint64_t> attempted_;
public:
    bool first_attempt(uint32_t world,uintptr_t mat,uint32_t cel) {
        if(!world || !mat || cel>=16U) return false;
        if(world_!=world) { attempted_.clear(); world_=world; }
        // This renderer targets 32-bit JK; material pointer is a full key.
        uint64_t const key=(uint64_t(uint32_t(mat))<<32)|uint64_t(cel);
        if(attempted_.size()>=4096U) return false;
        return attempted_.insert(key).second;
    }
    void clear() {attempted_.clear();world_=0;}
    std::size_t size() const {return attempted_.size();}
};
}
