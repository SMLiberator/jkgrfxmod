#pragma once
// Coalesce only immediately adjacent index ranges already grouped under
// identical draw state. Order and triangle sequence are unchanged, including
// in the depth-sorted alpha pass. No cross-Thing/material coalescing.
#include <cstdint>
#include <limits>
#include <vector>

namespace jkgm {
struct gpu_3do_index_run {
    uint32_t first_index=0;
    uint32_t count=0;
};

// Returns true iff the incoming range was absorbed into the previous run.
// Conservative 32-bit index/count checks ensure that a GLsizei draw cannot
// overflow when replacing glMultiDrawElements with glDrawElements.
inline bool append_gpu_3do_index_run(
    std::vector<gpu_3do_index_run> &runs,
    uint32_t first_index,uint32_t count)
{
    constexpr uint64_t max_draw_count=
        uint64_t((std::numeric_limits<int32_t>::max)());
    if(!runs.empty()) {
        auto &prev=runs.back();
        if(prev.count>0U && count>0U &&
           prev.count%3U==0U && count%3U==0U &&
           uint64_t(prev.first_index)+prev.count==first_index &&
           uint64_t(prev.count)+count<=max_draw_count) {
            prev.count+=count;
            return true;
        }
    }
    runs.push_back({first_index,count});
    return false;
}
} // namespace jkgm
