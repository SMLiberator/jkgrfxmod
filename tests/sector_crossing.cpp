#include "renderer/sector_crossing.hpp"
#include <cassert>
#include <cstdint>
#include <cstring>
#include <vector>

int main() {
    using namespace jkgm;
    auto room=[](uint32_t id,float lo,float hi) {
        sector_crossing_region s{};
        s.id=id;s.valid=true;s.adjacency_valid=true;
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
    snapshot.sectors[0].adjoining={1};
    snapshot.sectors[1].adjoining={0};
    snapshot.index_by_id={{11,0},{55,1}};
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

    // Native connectivity, not all geometry whose AABB happens to overlap:
    // owner 1 -> 8 -> 3. A disconnected 2 overlaps the Thing, but is
    // never visited. An unrelated distant 99 isn't visited either.
    sector_crossing_snapshot graph{};
    graph.sectors.push_back(room(1,-10,0));   // index 0
    graph.sectors.push_back(room(8,0,10));    // index 1
    graph.sectors.push_back(room(3,10,20));   // index 2
    graph.sectors.push_back(room(2,-5,15));   // index 3 (no adjoin)
    graph.sectors.push_back(room(99,100,110));// index 4 (far away)
    graph.sectors[0].adjoining={1};
    graph.sectors[1].adjoining={0,2};
    graph.sectors[2].adjoining={1};
    graph.index_by_id={{1,0},{8,1},{3,2},{2,3},{99,4}};
    graph.valid_count=5;
    auto path=sector_crossing_collect_candidates(graph,{{8,0,0}},10,1);
    assert(!path.overflow && path.count==3);
    assert(path.indices[0]==0 && path.indices[1]==1 &&
           path.indices[2]==2);
    assert(sector_crossing_select(graph,path,{{15,0,0}},1)==3);
    assert(sector_crossing_select(graph,path,{{-2,0,0}},1)==1);
    // No graph-wide scan to discover a disconnected overlap.
    // Mesh fully in sector 3, while its owning Thing is in 1:
    // sector 8 does not overlap this mesh but must still be traversed.
    auto submesh=sector_crossing_collect_candidates(graph,{{17,0,0}},1,1);
    assert(!submesh.overflow && submesh.count==2);
    assert(submesh.indices[0]==0 && submesh.indices[1]==2);
    assert(sector_crossing_select(graph,submesh,{{17,0,0}},1)==3);
    auto solo=sector_crossing_collect_candidates(graph,{{3,0,0}},1,2);
    assert(solo.count==1 && solo.indices[0]==3);
    // Stop at the first nonoverlapping intermediate sector: there is
    // no spatially plausible route from Thing to more remote rooms.
    auto short_path=sector_crossing_collect_candidates(graph,{{-4,0,0}},2,1);
    assert(short_path.count==1 && short_path.indices[0]==0);
    // Owner lookup must be direct and support arbitrary IDs.
    assert(sector_crossing_collect_candidates(
        graph,{{0,0,0}},5,9999).count==0);
    // Four includes owner: exactly four is permitted, a fifth causes a
    // safe fallback rather than an arbitrary partial set of sectors.
    graph.sectors.push_back(room(4,0,20));
    graph.sectors[1].adjoining.push_back(5);
    graph.index_by_id.emplace(4,5);
    auto four=sector_crossing_collect_candidates(graph,{{8,0,0}},10,1);
    assert(!four.overflow && four.count==4);
    graph.sectors.push_back(room(5,0,20));
    graph.sectors[1].adjoining.push_back(6);
    graph.index_by_id.emplace(5,6);
    auto overflow=sector_crossing_collect_candidates(graph,{{8,0,0}},10,1);
    assert(overflow.overflow && overflow.count==0);
    assert(sector_crossing_select(graph,overflow,{{15,0,0}},1)==1);
    // Incomplete reachable links never produce a partial candidate set.
    graph.sectors[1].adjacency_valid=false;
    auto invalid=sector_crossing_collect_candidates(graph,{{8,0,0}},10,1);
    assert(invalid.count==0 && !invalid.overflow);

    // Synthetic native JKGM layout, with two sector adjoin-linked records.
    // SithSurfaceAdjoin +0x04 is the destination, +0x10 is next.
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
        uint32_t a=adjoin+i*0x20U;
        u32(s,i?55:11);
        u32(s+0x24,1);
        u32(s+0x28,f);
        u32(s+0x2c,a);
        vec(s+0x70,{{i?0.0f:-10.0f,-5,-5}});
        vec(s+0x7c,{{i?10.0f:0.0f,5,5}});
        u32(f+0x08,s);
        u32(f+0x0c,a);
        u32(f+0x10,0);
        u32(f+0x2c,indices);
        vec(f+0x48,{{i?1.0f:-1.0f,0,0}});
        u32(a,2); // movement-enabled, regardless of surface collision flag
        u32(a+0x04,sector_base+(1-i)*0x98U);
        u32(a+0x10,0); // linked list termination
    }
    u32(indices,0);
    sector_crossing_snapshot extracted{};
    assert(sector_crossing_capture(read,world,extracted));
    assert(extracted.native_sector_count==2 && extracted.valid_count==2);
    assert(extracted.sectors[0].planes.size()==1);
    assert(extracted.sectors[1].planes.size()==1);
    assert(extracted.sectors[0].adjacency_valid);
    assert(extracted.sectors[1].adjacency_valid);
    assert(extracted.sectors[0].adjoining.size()==1 &&
           extracted.sectors[0].adjoining[0]==1);
    assert(extracted.sectors[1].adjoining.size()==1 &&
           extracted.sectors[1].adjoining[0]==0);
    auto exact=sector_crossing_collect_candidates(extracted,{{0,0,0}},2,11);
    assert(exact.count==2);
    assert(sector_crossing_select(extracted,exact,{{-1,0,0}},11)==11);
    assert(sector_crossing_select(extracted,exact,{{ 1,0,0}},11)==55);

    // Broken native adjoin destinations must NOT send the graph walk into
    // random memory or leave a partial set of candidate sectors.
    u32(adjoin+0x04,0x7fffffffU);
    assert(sector_crossing_capture(read,world,extracted));
    assert(!extracted.sectors[0].adjacency_valid);
    assert(sector_crossing_collect_candidates(
        extracted,{{0,0,0}},2,11).count==0);
    u32(adjoin+0x04,sector_base+0x98U);
    // A loop is bounded by numSurfaces; even a self-referencing next
    // record cannot hang level loading.
    u32(adjoin+0x10,adjoin);
    assert(sector_crossing_capture(read,world,extracted));
    assert(!extracted.sectors[0].adjacency_valid);
    u32(adjoin+0x10,0);
    // The existing face-geometry validator still rejects a bad index.
    u32(surfaces+0x2c,0x7fffffffU);
    assert(sector_crossing_capture(read,world,extracted));
    assert(extracted.valid_count==1 && !extracted.sectors[0].valid);
    return 0;
}
