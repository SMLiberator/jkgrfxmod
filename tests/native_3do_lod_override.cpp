// CPU-only regression for the per-rdThing native GeoSet 0 override.
// The actual hook brackets rdModel3_Draw, so nested model draws restore
// the exact previous per-Thing selection without touching rdModel3 state.
#include "renderer/native_3do_lod_override.hpp"
#include "renderer/native_3do_draw_memo.hpp"
#include <cassert>
#include <cstdint>
#include <cstring>
#include <unordered_map>

int main() {
    using namespace jkgm;
    struct draw_state { native_draw_memo<int> mode; };
    draw_state *active=nullptr;
    int reads=0,value=4;
    auto read_mode=[&] { ++reads;return value; };
    auto sample=[&] { return native_draw_read(active,&draw_state::mode,read_mode); };
    assert(sample()==4 && reads==1);
    {
        native_draw_scope<draw_state> outer(active,true);
        assert(sample()==4 && sample()==4 && reads==2);
        value=3;
        {
            native_draw_scope<draw_state> inner(active,true);
            assert(sample()==3 && sample()==3 && reads==3);
        }
        assert(sample()==3 && reads==4);
        *active={}; // native fallback invalidation
        assert(sample()==3 && reads==5);
        {
            native_draw_scope<draw_state> disabled(active,false);
            assert(sample()==3 && sample()==3 && reads==7);
        }
        assert(sample()==3 && reads==8);
    }
    assert(active==nullptr && sample()==3 && reads==9);
    std::unordered_map<uintptr_t,uint8_t> bytes;
    auto write=[&](uintptr_t ptr,uint32_t value) {
        uint8_t src[4]{};
        std::memcpy(src,&value,4);
        for(unsigned i=0;i<4;++i)bytes[ptr+i]=src[i];
    };
    auto read=[&](uintptr_t ptr,void *dst,size_t n) {
        auto *out=static_cast<uint8_t *>(dst);
        for(size_t i=0;i<n;++i) {
            auto it=bytes.find(ptr+i);
            if(it==bytes.end())return false;
            out[i]=it->second;
        }
        return true;
    };
    constexpr uintptr_t rdthing=0x20000U,model=0x30000U;
    write(rdthing+0x04U,uint32_t(model));
    write(model+0x44U,4U);
    write(model+0x50U,2U); // shared JK default selected LOD 2
    write(rdthing+0x30U,0xffffffffU); // per-Thing auto selection
    auto plan=prepare_native_3do_lod0_override(read,rdthing,model);
    assert(plan.valid && plan.selection_address==rdthing+0x30U);
    assert(plan.previous==-1);
    write(plan.selection_address,0U); // hook's temporary GeoSet 0
    uint32_t active=~0U,shared=~0U;
    assert(read(rdthing+0x30U,&active,4) && active==0U);
    assert(read(model+0x50U,&shared,4) && shared==2U);
    auto nested=prepare_native_3do_lod0_override(read,rdthing,model);
    assert(nested.valid && nested.previous==0);
    write(nested.selection_address,0U);
    write(nested.selection_address,uint32_t(nested.previous));
    write(plan.selection_address,uint32_t(plan.previous));
    assert(read(rdthing+0x30U,&active,4) && active==0xffffffffU);

    // Explicit per-Thing authored overrides are also bypassed while the
    // experiment runs, then preserved on return.
    write(rdthing+0x30U,3U);
    plan=prepare_native_3do_lod0_override(read,rdthing,model);
    assert(plan.valid && plan.previous==3);
    write(plan.selection_address,0U);
    write(plan.selection_address,uint32_t(plan.previous));
    assert(read(rdthing+0x30U,&active,4) && active==3U);
    // Source pointer mismatch and bad GeoSet bounds fail open.
    assert(!prepare_native_3do_lod0_override(
        read,rdthing,model+0x1000U).valid);
    write(model+0x44U,0U);
    assert(!prepare_native_3do_lod0_override(read,rdthing,model).valid);
    write(model+0x44U,4U);
    write(rdthing+0x30U,4U);
    assert(!prepare_native_3do_lod0_override(read,rdthing,model).valid);
    assert(!prepare_native_3do_lod0_override(read,0x123U,model).valid);
}
