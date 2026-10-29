#include "../renderer/cog_raycast_results.hpp"
#include <cassert>

int main()
{
    jkgm::cog_raycast_results results;
    jkgm::cog_raycast_hit hit{};
    hit.position={{1.0f,2.0f,3.0f}};
    hit.thing_index=42;
    hit.sector_index=7;
    // No world, no insertion.
    assert(results.add(0x10,hit)==-1);
    results.set_world(0x1234);
    assert(results.get(0x10,0)==nullptr);
    assert(results.get(0x10,-1)==nullptr);
    assert(results.get(0x10,5)==nullptr);
    for(int i=0;i<5;++i) {
        hit.distance=float(i+1);
        assert(results.add(0x10,hit)==i);
    }
    for(int i=0;i<5;++i)
        assert(results.get(0x10,i)->distance==float(i+1));
    hit.distance=99.0f;
    assert(results.add(0x10,hit)==0);
    assert(results.get(0x10,0)->distance==99.0f);
    assert(results.get(0x10,0)->sector_index==7);
    assert(results.get(0x10,1)->distance==2.0f);
    // Separate COG contexts have independent slot counters and results.
    assert(results.add(0x20,hit)==0);
    assert(results.get(0x20,0)->distance==99.0f);
    hit.distance=100.0f;
    assert(results.add(0x20,hit)==1);
    assert(results.get(0x10,1)->distance==2.0f);
    // Invalid requests must not advance the valid next slot.
    assert(results.add(0,hit)==-1);
    assert(results.get(0x10,0)->distance==99.0f);
    // Changing worlds invalidates every COG's results.
    results.set_world(0x5678);
    assert(results.get(0x10,0)==nullptr);
    assert(results.get(0x20,0)==nullptr);
    assert(results.add(0x10,hit)==0);
    // No-op world sync keeps results.
    results.set_world(0x5678);
    assert(results.get(0x10,0)->distance==100.0f);
    results.clear();
    assert(results.get(0x10,0)==nullptr);
}
