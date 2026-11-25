#include "renderer/gpu_3do_index_packing.hpp"
#include <cassert>
#include <cstdint>
#include <vector>

int main()
{
    using namespace jkgm;
    std::vector<uint16_t> packed{5U};
    std::vector<uint32_t> const triangles{0U,1U,2U,0U,2U,3U};
    assert(gpu_3do_pack_indices_u16(triangles,4U,packed));
    assert(packed.size()==triangles.size());
    for(size_t i=0;i<triangles.size();++i)
        assert(packed[i]==triangles[i]);
    // A model with 4096 native vertices can still have many more
    // cached GPU corners. 65536 is the last compatible corner count.
    std::vector<uint32_t> const edge{0U,65535U,17U};
    assert(gpu_3do_pack_indices_u16(edge,65536U,packed));
    assert(packed[1]==65535U);
    assert(!gpu_3do_pack_indices_u16(edge,65537U,packed));
    assert(packed.empty());
    assert(!gpu_3do_pack_indices_u16(edge,65535U,packed));
    assert(packed.empty());
    assert(!gpu_3do_pack_indices_u16({0U,1U,65536U},65536U,packed));
    assert(packed.empty());
    assert(!gpu_3do_pack_indices_u16({0U,1U},3U,packed));
    assert(!gpu_3do_pack_indices_u16({},3U,packed));
    assert(!gpu_3do_pack_indices_u16({0U,1U,2U},0U,packed));
    return 0;
}
