#pragma once
// Short-lived memo of memory regions already proven readable.
//
// Every native read the mod performs is guarded by a "is this range committed
// and accessible?" check, which is a VirtualQuery() call. JK keeps its meshes,
// faces, materials and Things in a handful of large heap regions, so busy
// scenes ask the OS the same question about the same few regions thousands of
// times per frame - and a VirtualQuery() costs far more under Wine than on
// Windows. This remembers a region once it has been validated so that later
// reads inside it skip the system call.
//
// Safety model (matches the per-frame cache already used for 3DO meshes):
//   * Only POSITIVE results are cached; a failed check is always re-run.
//   * An entry is valid only for one "epoch" (the caller passes a coarse time
//     bucket), so a region the engine frees or decommits is re-validated within
//     a few milliseconds, never carried across frames.
//   * No pointers or data are retained, only [begin,end) bounds.
//
// This header has no Windows dependency so it can be unit-tested anywhere; the
// caller supplies the actual query.
#include <array>
#include <cstddef>
#include <cstdint>
#include <utility>

namespace jkgm {
    class readable_region_cache {
    public:
        static constexpr size_t capacity = 64;

        struct region {
            uintptr_t begin = 0, end = 0;
            uint32_t epoch = 0;
            bool valid = false;
        };

        uint64_t hits = 0, queries = 0;

        // query(address, begin, end) must return true only if `address` lies
        // in committed, accessible memory, and then report the bounds of the
        // whole region that shares those attributes.
        template <class Query>
        bool readable(uintptr_t address, size_t size, uint32_t epoch, Query &&query)
        {
            uintptr_t const last = address + size;
            if(last < address) {
                return false; // range wraps the address space
            }
            for(size_t i = 0; i < capacity; ++i) {
                region &r = regions[i];
                if(r.valid && r.epoch == epoch && address >= r.begin && address < r.end) {
                    ++hits;
                    // The remembered bounds are the whole run of pages sharing
                    // these attributes, so a range that leaves them is not
                    // readable as one block - exactly what the system call
                    // would report, without making it.
                    bool const inside = last <= r.end;
                    if(i != 0) {
                        std::swap(regions[i], regions[0]); // keep hot regions first
                    }
                    return inside;
                }
            }

            ++queries;
            uintptr_t begin = 0, end = 0;
            if(!query(address, begin, end) || end <= begin || address < begin) {
                return false;
            }
            // Remember the region even when this particular range overruns it,
            // so a following in-range read still hits.
            insert(region{begin, end, epoch, true});
            return last <= end;
        }

        void clear()
        {
            for(auto &r : regions) {
                r.valid = false;
            }
        }

    private:
        std::array<region, capacity> regions{};
        size_t next = 0;

        void insert(region const &r)
        {
            // Replace an expired/overlapping entry for the same region first.
            for(auto &e : regions) {
                if(e.valid && e.begin == r.begin && e.end == r.end) {
                    e = r;
                    return;
                }
            }
            regions[next++ % capacity] = r;
        }
    };
}
