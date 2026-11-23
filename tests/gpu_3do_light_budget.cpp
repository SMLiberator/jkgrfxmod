#include "renderer/gpu_3do_preview.hpp"
#include <cassert>
#include <array>
using namespace jkgm;
int main() {
    std::array<gpu_3do_light_priority,64> entries{};
    auto result=select_gpu_3do_light_budget(entries,0U,0.0f,0.0f);
    assert(result.count==0U && result.clipped==0U);

    // No reordering or clipping within the existing shader budget.
    for(unsigned i=0;i<64;++i)
        entries[i]={1.0f,100.0f,float(i),i};
    result=select_gpu_3do_light_budget(entries,16U,0.0f,1.0f);
    assert(result.count==16U && result.clipped==0U);
    for(unsigned i=0;i<16;++i)assert(result.indices[i]==i);

    // The 17th light must replace a weak one, not reject the geometry.
    for(unsigned i=0;i<17;++i)
        entries[i]={1.0f,100.0f,1.0f,i};
    entries[0].intensity=0.1f;
    entries[16].intensity=10.0f;
    result=select_gpu_3do_light_budget(entries,17U,0.0f,0.0f);
    assert(result.count==16U && result.clipped==1U);
    for(unsigned i=0;i<16;++i)
        assert(result.indices[i]==i+1U);

    // With attenuation, prefer a nearby light to a distant light.
    for(unsigned i=0;i<17;++i)
        entries[i]={2.0f,100.0f,1.0f,i};
    entries[0].distance_squared=900.0f;
    entries[16].distance_squared=0.0f;
    result=select_gpu_3do_light_budget(entries,17U,2.0f,1.0f);
    assert(result.count==16U && result.clipped==1U);
    for(unsigned i=0;i<16;++i)assert(result.indices[i]==i+1U);

    // All 64 native camera slots remain safe, with exact top 16 retained.
    for(unsigned i=0;i<64;++i)
        entries[i]={float(i+1U),100.0f,0.0f,i};
    result=select_gpu_3do_light_budget(entries,64U,0.0f,0.0f);
    assert(result.count==16U && result.clipped==48U);
    for(unsigned i=0;i<16;++i)assert(result.indices[i]==i+48U);

    // A corrupted native count cannot index beyond the 64-slot array.
    result=select_gpu_3do_light_budget(entries,100U,0.0f,0.0f);
    assert(result.count==16U && result.clipped==48U);

    // Source reorder does not change the native-ID tie-break preference.
    for(unsigned i=0;i<17;++i)
        entries[i]={1.0f,100.0f,0.0f,16U-i};
    result=select_gpu_3do_light_budget(entries,17U,0.0f,0.0f);
    assert(result.clipped==1U);
    assert(result.indices[0]==1U);
    for(unsigned i=1;i<16;++i)assert(result.indices[i]==i+1U);
    return 0;
}
