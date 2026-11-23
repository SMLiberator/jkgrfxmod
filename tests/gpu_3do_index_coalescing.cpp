#include "renderer/gpu_3do_index_coalescing.hpp"
#include <cassert>
#include <cstdint>
#include <limits>
#include <vector>

using namespace jkgm;

static std::vector<uint32_t> expanded(
    std::vector<gpu_3do_index_run> const &runs) {
    std::vector<uint32_t> out;
    for(auto const &r:runs)
        for(uint32_t i=0;i<r.count;++i)
            out.push_back(r.first_index+i);
    return out;
}

int main() {
    std::vector<gpu_3do_index_run> runs;
    assert(!append_gpu_3do_index_run(runs,0U,3U));
    assert(append_gpu_3do_index_run(runs,3U,6U));
    assert(append_gpu_3do_index_run(runs,9U,3U));
    assert(runs.size()==1U && runs[0].first_index==0U &&
           runs[0].count==12U);
    // A small EBO gap must NOT be filled with triangles from another MAT.
    assert(!append_gpu_3do_index_run(runs,18U,6U));
    assert(runs.size()==2U && runs[1].first_index==18U);
    assert(append_gpu_3do_index_run(runs,24U,3U));
    assert(runs[1].count==9U);

    // Transparent painter order is authoritative. Never reorder ranges
    // merely because the same underlying EBO has adjacent offsets.
    runs.clear();
    std::vector<gpu_3do_index_run> const alpha_source{
        {30U,6U},{0U,6U},{6U,3U},{39U,3U},{36U,3U}};
    auto const original=expanded(alpha_source);
    for(auto const &r:alpha_source)
        append_gpu_3do_index_run(runs,r.first_index,r.count);
    assert(runs.size()==4U);
    assert(runs[0].first_index==30U && runs[0].count==6U);
    assert(runs[1].first_index==0U && runs[1].count==9U);
    assert(runs[2].first_index==39U && runs[3].first_index==36U);
    assert(expanded(runs)==original);

    // A pair of reversed, duplicated or overlapping spans must not merge.
    runs.clear();
    assert(!append_gpu_3do_index_run(runs,12U,6U));
    assert(!append_gpu_3do_index_run(runs,9U,6U));
    assert(!append_gpu_3do_index_run(runs,9U,6U));
    assert(!append_gpu_3do_index_run(runs,12U,6U));
    assert(runs.size()==4U);

    // Guard the signed GLsizei limit (including when both inputs are
    // individually valid), 32-bit offset wrap, non-triangle counts and
    // empty ranges. Do not silently change previously legal draw calls.
    runs.clear();
    constexpr uint32_t max_tri_count=2147483646U;
    assert(!append_gpu_3do_index_run(runs,0U,max_tri_count));
    assert(!append_gpu_3do_index_run(runs,max_tri_count,3U));
    assert(runs.size()==2U && runs[0].count==max_tri_count);
    runs.clear();
    assert(!append_gpu_3do_index_run(runs,0xfffffffcU,3U));
    assert(!append_gpu_3do_index_run(runs,0U,3U));
    assert(runs.size()==2U);
    runs.clear();
    assert(!append_gpu_3do_index_run(runs,0U,3U));
    assert(!append_gpu_3do_index_run(runs,3U,2U));
    assert(!append_gpu_3do_index_run(runs,5U,3U));
    assert(!append_gpu_3do_index_run(runs,8U,0U));
    assert(runs.size()==4U);
    return 0;
}
