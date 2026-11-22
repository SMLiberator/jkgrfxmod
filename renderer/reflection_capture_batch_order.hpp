#pragma once
#include <algorithm>
#include <cstddef>
#include <vector>

namespace jkgm {
    struct reflection_capture_batch_order_stats {
        std::size_t opaque_ranges=0;
        std::size_t source_order_ranges=0;
        std::size_t sorted_spans=0;
    };
    // Sort only uninterrupted fully opaque runs. Masked and translucent
    // groups are barriers: their authored order and position never change.
    template<typename T,typename IsOpaque,typename Less>
    reflection_capture_batch_order_stats
    order_reflection_capture_opaque_spans(
            std::vector<T> &groups,IsOpaque is_opaque,Less less) {
        reflection_capture_batch_order_stats stats{};
        for(std::size_t start=0;start<groups.size();) {
            if(!is_opaque(groups[start])) {
                ++stats.source_order_ranges;
                ++start;
                continue;
            }
            std::size_t end=start+1U;
            while(end<groups.size() && is_opaque(groups[end]))++end;
            stats.opaque_ranges+=end-start;
            // Sorting 1-2 ranges cannot decrease draw calls.
            if(end-start>=3U) {
                std::sort(groups.begin()+start,groups.begin()+end,less);
                ++stats.sorted_spans;
            }
            start=end;
        }
        return stats;
    }
}
