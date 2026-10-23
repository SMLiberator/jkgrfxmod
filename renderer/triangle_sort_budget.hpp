#pragma once
// Bounded-work guard for the native translucent triangle painter-sort.
// Header deliberately uses only the C++ standard library so adversarial
// fallback cases can be tested independently of OpenGL and Sith Engine math.
#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <iterator>

namespace jkgm {
    struct triangle_sort_budget {
        // Each normal partition performs O(n) classification and one
        // O(n log n) std::sort. Cap the sum of n over all recursive partitions
        // to prevent near-coplanar faces degenerating to O(n^2 log n).
        static constexpr size_t max_scanned = 98304U;
        static constexpr unsigned max_depth = 48U;
        static constexpr unsigned max_stalled_pivots = 8U;

        size_t scanned = 0U;
        uint64_t fallback_groups = 0U;
        uint64_t fallback_triangles = 0U;

        bool permit(size_t count, unsigned depth, unsigned stalled) {
            if(count < 2U) return true;
            if(depth >= max_depth || stalled >= max_stalled_pivots ||
               count > max_scanned - scanned) return false;
            scanned += count;
            return true;
        }

        // Only fall back within the current unresolved partition, never
        // across successfully ordered geometric partitions or the preceding
        // depth-writing alpha-masked range. The key is the caller's finite
        // eye-space depth (larger W first, consistent with fast partition).
        template <typename It, typename DepthKey>
        void fallback(It first, It last, DepthKey const &key) {
            auto const count=static_cast<size_t>(std::distance(first,last));
            if(count<2U) return;
            ++fallback_groups;
            fallback_triangles+=count;
            std::sort(first,last,[&](auto const &a,auto const &b){
                return key(a)>key(b);
            });
        }
    };
}
