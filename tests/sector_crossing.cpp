#include "renderer/sector_crossing.hpp"
#include <cassert>
#include <cstdint>
#include <cstring>
#include <vector>

int main() {
    using namespace jkgm;
    auto room=[](uint32_t id,float lo,float hi) {
        sector_crossing_region s{};
        s.id=id;s.valid=true;
        s.bounds_min={{lo,-5,-5}};
        s.bounds_max={{hi,5,5}};
        s.planes.push_back({{{1,0,0}},-lo});
        s.planes.push_back({{{-1,0,0}},hi});
        s.planes.push_back({{{0,1,0}},5});
        s.planes.push_back({{{0,-1,0}},5});
        s.planes.push_back({{{0,0,1}},5});
        s.planes.push_back({{{0,0,-1}},5});
        return s;
    };
    sector_crossing_snapshot snapshot{};
    snapshot.sectors.push_back(room(11,-10,0));
    snapshot.sectors.push_back(room(55,0,10));
    snapshot.valid_count=2;
    auto c=sector_crossing_collect_candidates(snapshot,{{0,0,0}},2,11);
    assert(!c.overflow && c.count==2);
    assert(sector_crossing_select(snapshot,c,{{-2,0,0}},11)==11);
    assert(sector_crossing_select(snapshot,c,{{ 2,0,0}},11)==55);
    assert(sector_crossing_select(snapshot,c,{{ 0,0,0}},11)==11);
    assert(sector_crossing_select(snapshot,c,{{20,0,0}},11)==11);
    assert(sector_crossing_collect_candidates(
        snapshot,{{-7,0,0}},1,11).count==1);
    auto slanted=room(77,0,10);
    slanted.planes.push_back({{{-0.70710678f,0.70710678f,0}},0});
    assert(!sector_crossing_contains(slanted,{{4,0,0}}));
    assert(sector_crossing_contains(slanted,{{1,4,0}}));
    slanted.has_interior_box=true;
    slanted.interior_min={{3,-1,-1}};
    slanted.interior_max={{5,1,1}};
    assert(sector_crossing_contains(slanted,{{4,0,0}}));
    for(int i=0;i<8;++i)snapshot.sectors.push_back(room(100+i,-1,1));
    auto overflow=sector_crossing_collect_candidates(
        snapshot,{{0,0,0}},2,11);
    assert(overflow.overflow && overflow.count==0);
    assert(sector_crossing_select(snapshot,overflow,{{1,0,0}},11)==11);

    // Synthetic native JKGM layout: two sectors share a movement-enabled
    // adjoin plane but use distinct sparse sector IDs (11 and 55).
    constexpr uint32_t world=0x10000,sector_base=0x20000;
    constexpr uint32_t vertices=0x30000,surfaces=0x40000;
    constexpr uint32_t indices=0x50000,adjoin=0x60000;
    std::vector<uint8_t> bytes(0x70000U,0);
    auto u32=[&](size_t p,uint32_t v) {
        std::memcpy(bytes.data()+p,&v,sizeof(v));
    };
    auto vec=[&](size_t p,sector_crossing_vec3 v) {
        std::memcpy(bytes.data()+p,v.data(),12);
    };
    auto read=[&](uintptr_t p,void *out,size_t n) {
        if(p>bytes.size() || n>bytes.size()-p)return false;
        std::memcpy(out,bytes.data()+p,n);return true;
    };
    u32(world+0x4c,2);
    u32(world+0x50,sector_base);
    u32(world+0x88,1);
    u32(world+0x8c,vertices);
    u32(world+0xa8,2);
    u32(world+0xac,surfaces);
    vec(vertices,{{0,0,0}});
    for(uint32_t i=0;i<2;++i) {
        uint32_t s=sector_base+i*0x98U,f=surfaces+i*0x5cU;
        u32(s,i?55:11);
        u32(s+0x24,1);
        u32(s+0x28,f);
        vec(s+0x70,{{i?0.0f:-10.0f,-5,-5}});
        vec(s+0x7c,{{i?10.0f:0.0f,5,5}});
        u32(f+0x08,s);
        u32(f+0x0c,adjoin);
        u32(f+0x10,0);
        u32(f+0x2c,indices);
        vec(f+0x48,{{i?1.0f:-1.0f,0,0}});
    }
    u32(indices,0);
    u32(adjoin,2); // movement-enabled surface, not a collision flag
    sector_crossing_snapshot extracted{};
    assert(sector_crossing_capture(read,world,extracted));
    assert(extracted.native_sector_count==2 && extracted.valid_count==2);
    assert(extracted.sectors[0].planes.size()==1);
    assert(extracted.sectors[1].planes.size()==1);
    auto exact=sector_crossing_collect_candidates(extracted,{{0,0,0}},2,11);
    assert(exact.count==2);
    assert(sector_crossing_select(extracted,exact,{{-1,0,0}},11)==11);
    assert(sector_crossing_select(extracted,exact,{{ 1,0,0}},11)==55);
    u32(surfaces+0x2c,0x7fffffffU);
    assert(sector_crossing_capture(read,world,extracted));
    assert(extracted.valid_count==1 && !extracted.sectors[0].valid);
    return 0;
}
