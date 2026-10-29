// Tests for the shared validated-region memo (no Windows needed).
#include "renderer/readable_region_cache.hpp"
#include <cassert>
#include <cstdint>
#include <vector>

namespace {
    struct fake_region { uintptr_t begin, end; bool readable; };
    struct fake_memory {
        std::vector<fake_region> regions;
        unsigned calls = 0;
        bool operator()(uintptr_t a, uintptr_t &b, uintptr_t &e) {
            ++calls;
            for(auto const &r : regions) {
                if(a >= r.begin && a < r.end) {
                    if(!r.readable) return false;
                    b = r.begin; e = r.end;
                    return true;
                }
            }
            return false;
        }
    };
}

int main()
{
    using jkgm::readable_region_cache;
    fake_memory mem;
    mem.regions = {{0x100000, 0x200000, true},
                   {0x300000, 0x310000, true},
                   {0x400000, 0x410000, false}};
    readable_region_cache cache;

    // First touch queries; everything else in that region is free.
    assert(cache.readable(0x100100, 16, 1, mem));
    assert(mem.calls == 1);
    for(uintptr_t a = 0x100000; a < 0x1fffc0; a += 0x1234)
        assert(cache.readable(a, 64, 1, mem));
    assert(mem.calls == 1);

    // A range that runs off the end of the region is rejected, not cached as OK.
    assert(!cache.readable(0x1fffff, 16, 1, mem));
    // ...but it must not poison the region: in-range reads still pass.
    assert(cache.readable(0x1ffff0, 16, 1, mem));
    assert(mem.calls == 1);

    // Unreadable memory is never cached: it is re-queried every time.
    unsigned const before = mem.calls;
    assert(!cache.readable(0x400010, 4, 1, mem));
    assert(!cache.readable(0x400010, 4, 1, mem));
    assert(mem.calls == before + 2);
    // Unmapped address.
    assert(!cache.readable(0x900000, 4, 1, mem));

    // A new epoch forces re-validation (memory may have been freed meanwhile).
    unsigned const e1 = mem.calls;
    assert(cache.readable(0x100100, 16, 2, mem));
    assert(mem.calls == e1 + 1);
    assert(cache.readable(0x100200, 16, 2, mem));
    assert(mem.calls == e1 + 1);

    // If the region vanished between epochs, the stale entry must NOT be used.
    mem.regions[0].readable = false;
    assert(!cache.readable(0x100100, 16, 3, mem));
    mem.regions[0].readable = true;

    // Two regions coexist; neither evicts the other.
    cache.clear();
    mem.calls = 0;
    for(int i = 0; i < 100; ++i) {
        assert(cache.readable(0x100100 + i, 4, 5, mem));
        assert(cache.readable(0x300100 + i, 4, 5, mem));
    }
    assert(mem.calls == 2);

    // Address-space wraparound is rejected outright.
    assert(!cache.readable(~uintptr_t(0) - 2, 16, 5, mem));

    // More distinct regions than slots still works (round-robin replacement).
    fake_memory many;
    for(uintptr_t i = 0; i < 200; ++i)
        many.regions.push_back({0x1000000 + i * 0x10000, 0x1000000 + i * 0x10000 + 0x8000, true});
    readable_region_cache small;
    for(int pass = 0; pass < 3; ++pass)
        for(uintptr_t i = 0; i < 200; ++i)
            assert(small.readable(0x1000000 + i * 0x10000 + 8, 8, 9, many));
    return 0;
}
