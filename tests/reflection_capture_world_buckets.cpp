#include "renderer/reflection_capture_world_buckets.hpp"
#include <cassert>
#include <cstdint>
#include <tuple>
#include <vector>

struct test_surface {
    uint32_t count=3;
    uintptr_t material=0;
    int32_t cel=0;
    bool has_uv=true;
    bool passable_adjoin=false;
    uint32_t surface_flags=0;
};

int main() {
    jkgm::reflection_capture_world_buckets cache;
    // Interleave materials to verify source order within sorted buckets.
    std::vector<test_surface> surfaces{
        {3,20,0,true,false,0},
        {3,10,1,true,false,0},
        {3,20,0,true,false,0},
        {3,20,0,false,false,0},
        {2,20,0,true,false,0},    // invalid polygon
        {3,20,0,true,true,0},     // passable adjoin
        {3,20,0,true,false,0x200},// sky horizon
        {3,20,0,true,false,0x400},// sky ceiling
        {3,0,0,true,false,0},     // no material
        {3,10,1,true,false,0}
    };
    assert(cache.refresh(0x1234U,1,surfaces));
    auto const &groups=cache.buckets();
    assert(groups.size()==3);
    assert(groups[0].key==std::make_tuple(uintptr_t(10),int32_t(1),false));
    assert((groups[0].surfaces==std::vector<size_t>{1,9}));
    assert(groups[1].key==std::make_tuple(uintptr_t(20),int32_t(0),false));
    assert((groups[1].surfaces==std::vector<size_t>{0,2}));
    assert(groups[2].key==std::make_tuple(uintptr_t(20),int32_t(0),true));
    assert((groups[2].surfaces==std::vector<size_t>{3}));
    assert(!cache.refresh(0x1234U,1,surfaces));
    surfaces[0].material=30;
    assert(!cache.refresh(0x1234U,1,surfaces));
    assert(cache.refresh(0x5678U,1,surfaces));
    assert(cache.buckets().size()==4);
    assert(cache.refresh(0x5678U,2,surfaces));
    surfaces.push_back({3,30,0,true,false,0});
    assert(cache.refresh(0x5678U,2,surfaces));
    cache.invalidate();
    assert(cache.buckets().empty());
    assert(cache.refresh(0x5678U,2,surfaces));
    return 0;
}
