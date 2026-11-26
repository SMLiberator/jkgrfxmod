// CPU-only regression for the per-rdThing native GeoSet 0 override.
// The actual hook brackets rdModel3_Draw, so nested model draws restore
// the exact previous per-Thing selection without touching rdModel3 state.
#include "renderer/native_3do_lod_override.hpp"
#include "renderer/native_3do_draw_memo.hpp"
#include "renderer/native_3do_direct_model_audit.hpp"
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
    uint32_t active_geoset=~0U,shared=~0U;
    assert(read(rdthing+0x30U,&active_geoset,4) && active_geoset==0U);
    assert(read(model+0x50U,&shared,4) && shared==2U);
    auto nested=prepare_native_3do_lod0_override(read,rdthing,model);
    assert(nested.valid && nested.previous==0);
    write(nested.selection_address,0U);
    write(nested.selection_address,uint32_t(nested.previous));
    write(plan.selection_address,uint32_t(plan.previous));
    assert(read(rdthing+0x30U,&active_geoset,4) && active_geoset==0xffffffffU);

    // Explicit per-Thing authored overrides are also bypassed while the
    // experiment runs, then preserved on return.
    write(rdthing+0x30U,3U);
    plan=prepare_native_3do_lod0_override(read,rdthing,model);
    assert(plan.valid && plan.previous==3);
    write(plan.selection_address,0U);
    write(plan.selection_address,uint32_t(plan.previous));
    assert(read(rdthing+0x30U,&active_geoset,4) && active_geoset==3U);
    // Source pointer mismatch and bad GeoSet bounds fail open.
    assert(!prepare_native_3do_lod0_override(
        read,rdthing,model+0x1000U).valid);
    write(model+0x44U,0U);
    assert(!prepare_native_3do_lod0_override(read,rdthing,model).valid);
    write(model+0x44U,4U);
    write(rdthing+0x30U,4U);
    assert(!prepare_native_3do_lod0_override(read,rdthing,model).valid);
    assert(!prepare_native_3do_lod0_override(read,0x123U,model).valid);

    // Direct-model audit must follow child/sibling links rather than node
    // array order, retain repeated meshes and ignore the root cut flag.
    constexpr uint32_t hierarchy=0x40000U,meshes=0x50000U,cuts=0x60000U;
    write(model+0x44,1);write(model+0x50,0);
    write(model+0x54,4);write(model+0x58,hierarchy);
    write(model+0x24,2);write(model+0x28,meshes);
    write(rdthing+0x30,0xffffffffU);write(rdthing+0x28,cuts);
    auto node=[&](uint32_t index,uint32_t mesh,uint32_t n,uint32_t child,uint32_t sibling) {
        auto const addr=hierarchy+index*0xb4;
        write(addr+0x44,index);write(addr+0x4c,mesh);write(addr+0x54,n);
        write(addr+0x58,child);write(addr+0x5c,sibling);
    };
    node(0,0,2,hierarchy+2*0xb4,0);
    node(2,1,1,hierarchy+3*0xb4,hierarchy+0xb4);
    node(3,0,0,0,0);node(1,1,0,0,0);
    for(uint32_t i=0;i<4;++i)write(cuts+i*4,0);
    write(cuts,1);
    direct_model_plan draw_plan{};
    assert(predict_direct_model_nodes(read,uint32_t(rdthing),draw_plan));
    assert(draw_plan.nodes.size()==4 && draw_plan.nodes[0].node==0 &&
           draw_plan.nodes[1].node==2 && draw_plan.nodes[2].node==3 &&
           draw_plan.nodes[3].node==1);
    assert(draw_plan.nodes[1].mesh==meshes+0x70 &&
           draw_plan.nodes[2].mesh==meshes);
    write(cuts+8,1);
    assert(predict_direct_model_nodes(read,uint32_t(rdthing),draw_plan));
    assert(draw_plan.nodes.size()==2 && draw_plan.nodes[1].node==1);
    // Full topology can be reused by a second Thing with different cuts.
    direct_model_topology_cache topology;
    topology.prepare(1,1,true);
    bool hit=false;
    auto const *full=topology.get(read,uint32_t(rdthing),hit);
    assert(full && !hit && full->nodes.size()==4);
    // Direct automatic LOD ignores both native selectors without writing
    // either one. Even an invalid native selection cannot affect GeoSet 0.
    write(rdthing+0x30,99U);
    assert(!topology.get(read,uint32_t(rdthing),hit));
    assert(topology.get(read,uint32_t(rdthing),hit,true)==full && hit);
    assert(read(rdthing+0x30,&active_geoset,4) && active_geoset==99U);
    write(rdthing+0x30,0xffffffffU);
    uint32_t live_cuts[4]={1,0,1,0};
    std::vector<uint32_t> order;
    auto visit=[&](direct_model_node const &n) {order.push_back(n.node);return true;};
    assert(visit_direct_model_meshes(*full,live_cuts,visit));
    assert((order==std::vector<uint32_t>{0,1}));
    direct_model_prepared_cache<unsigned> prepared;
    prepared.prepare(1,2,3);
    auto &slots=prepared.get(*full);
    assert(slots.size()==4 && slots[2]==0);
    slots[2]=42;
    assert(prepared.get(*full)[2]==42);
    prepared.prepare(1,2,3);
    assert(prepared.get(*full)[2]==42);
    prepared.prepare(2,2,3);
    assert(prepared.get(*full)[2]==0);
    prepared.get(*full)[2]=42;
    prepared.prepare(2,3,3);
    assert(prepared.get(*full)[2]==0);
    prepared.get(*full)[2]=42;
    prepared.prepare(2,3,4);
    assert(prepared.get(*full)[2]==0);
    auto other=*full;
    other.geoset_address+=8;
    prepared.get(other)[2]=7;
    assert(prepared.get(*full)[2]==0);
    assert(prepared.get(other)[2]==7);
    live_cuts[2]=0;order.clear();
    auto const *again=topology.get(read,uint32_t(rdthing),hit);
    assert(again==full && hit);
    assert(visit_direct_model_meshes(*again,live_cuts,visit));
    assert((order==std::vector<uint32_t>{0,2,3,1}));
    assert(!visit_direct_model_meshes(*again,live_cuts,
        [](direct_model_node const &n) {return n.node!=2;}));
    node(2,0xffffffffU,1,hierarchy+3*0xb4,hierarchy+0xb4);
    topology.prepare(2,1,true);
    full=topology.get(read,uint32_t(rdthing),hit);
    assert(full && !hit);
    live_cuts[2]=1;order.clear();
    assert(visit_direct_model_meshes(*full,live_cuts,visit));
    assert((order==std::vector<uint32_t>{0,1}));
    write(cuts+8,0);write(cuts,0);
    node(2,1,1,hierarchy,hierarchy+0xb4);
    topology.prepare(3,1,true);
    assert(!topology.get(read,uint32_t(rdthing),hit));
    assert(!predict_direct_model_nodes(read,uint32_t(rdthing),draw_plan));
    assert(draw_plan.nodes.empty());
    auto dm=predict_direct_model_modes({4,3,2},{3,2,1},2,1.0f);
    assert(dm.geo==3 && dm.light==0 && dm.tex==1);
    dm=predict_direct_model_modes({4,3,2},{3,2,1},2,0.9f);
    assert(dm.light==2);
}
