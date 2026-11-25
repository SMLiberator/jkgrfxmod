#pragma once
// GPU-side 16-bit element-buffer eligibility for indexed 3DO snapshots.
// The canonical CPU mesh retains 32-bit indices for caches and geometry edits.
#include <cstddef>
#include <cstdint>
#include <limits>
#include <vector>

namespace jkgm {
inline bool gpu_3do_pack_indices_u16(
    std::vector<uint32_t> const &source,size_t vertex_count,
    std::vector<uint16_t> &packed)
{
    packed.clear();
    if(source.empty() || source.size()%3U!=0U ||
       vertex_count==0U ||
       vertex_count>size_t(std::numeric_limits<uint16_t>::max())+1U)
        return false;
    // Verify the complete index stream BEFORE converting. 4096 native
    // positions may expand to >65536 face corners due to UV/face seams.
    for(uint32_t index:source)
        if(index>=vertex_count ||
           index>std::numeric_limits<uint16_t>::max())
            return false;
    packed.reserve(source.size());
    for(uint32_t index:source)
        packed.push_back(static_cast<uint16_t>(index));
    return true;
}
} // namespace jkgm
