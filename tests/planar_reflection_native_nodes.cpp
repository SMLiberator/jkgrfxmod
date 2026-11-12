#include "renderer/planar_reflection_native_nodes.hpp"
#include <cassert>
#include <cstdint>
#include <cstring>
#include <unordered_map>
#include <vector>

int main()
{
    using namespace jkgm;
    std::unordered_map<uintptr_t,uint8_t> bytes;
    auto put=[&](uintptr_t at,auto const &v) {
        auto const *p=reinterpret_cast<uint8_t const *>(&v);
        for(size_t i=0;i<sizeof(v);++i)bytes[at+i]=p[i];
    };
    auto read=[&](uintptr_t at,void *dest,size_t n)->bool {
        auto *p=static_cast<uint8_t*>(dest);
        for(size_t i=0;i<n;++i) {
            auto it=bytes.find(at+i);
            if(it==bytes.end())return false;
            p[i]=it->second;
        }
        return true;
    };
    constexpr uintptr_t thing=0x20000U,rd=thing+0xc4U;
    constexpr uintptr_t model=0x30000U;
    constexpr uintptr_t hierarchy=0x40000U,meshes=0x50000U;
    constexpr uintptr_t pose=0x60000U,cuts=0x70000U;
    auto const code=jkgm_node_matrix_prologue;
    for(size_t i=0;i<code.size();++i)
        bytes[jkgm_node_matrix_address+i]=code[i];
    put(thing+0x0cU,uint32_t(2U)); // Ordinary SithThing.
    put(thing+0x74U,uint32_t(23U));
    gpu_3do_matrix34 const identity{{1,0,0,0,1,0,0,0,1,0,0,0}};
    // resolve_native_thing_node checks the full 48-byte orientation block
    // before replacing its translation with SithThing.position (+0xb8).
    for(size_t i=0;i<12;++i)put(thing+0x88U+i*4U,identity[i]);
    for(size_t i=0;i<3;++i)put(thing+0xb8U+i*4U,identity[9+i]);
    put(rd,uint32_t(1U));
    put(rd+4U,uint32_t(model));
    put(rd+0x44U,uint32_t(thing));
    put(rd+0x20U,uint32_t(pose));
    put(rd+0x28U,uint32_t(cuts));
    put(rd+0x30U,int32_t(1)); // Use GeoSet 1, not model default.
    put(model+0x44U,uint32_t(2));
    put(model+0x50U,uint32_t(0));
    put(model+0x54U,uint32_t(4));
    put(model+0x58U,uint32_t(hierarchy));
    put(model+0x24U,uint32_t(1));
    put(model+0x28U,uint32_t(0x51000U));
    put(model+0x2cU,uint32_t(3));
    put(model+0x30U,uint32_t(meshes));
    for(uint32_t i=0;i<4U;++i) {
        uintptr_t const node=hierarchy+uintptr_t(i)*0xb4U;
        put(node+0x44U,i);
        put(node+0x4cU,i==3U ? 0U : i);
        uint32_t parent=0;
        if(i==1U)parent=uint32_t(hierarchy);
        if(i==2U)parent=uint32_t(hierarchy+0xb4U);
        if(i==3U)parent=uint32_t(hierarchy);
        put(node+0x50U,parent);
        put(cuts+uintptr_t(i)*4U,uint32_t(i==1U?1U:0U));
        gpu_3do_matrix34 m=identity;
        m[9]=float(i*10U);
        for(size_t n=0;n<m.size();++n)
            put(pose+uintptr_t(i)*sizeof(m)+n*sizeof(float),m[n]);
    }
    planar_reflection_model_thing source{};
    source.thing=thing;
    source.model=model;
    source.signature=23U;
    unsigned refreshes=0U;
    auto refresh=[&](uintptr_t r,gpu_3do_matrix34 const &placement,
                     gpu_3do_matrix34 &first)->bool {
        assert(r==rd && placement==identity);
        ++refreshes;
        first=identity;
        return true;
    };
    std::vector<planar_reflection_native_node> nodes;
    assert(collect_planar_reflection_native_nodes(read,source,refresh,nodes));
    assert(refreshes==1U && nodes.size()==2U);
    assert(nodes[0].mesh==meshes && nodes[1].mesh==meshes);
    assert(nodes[0].hierarchy_index==0 && nodes[1].hierarchy_index==3);
    assert(nodes[0].geoset==1U && nodes[1].geoset==1U);
    assert(nodes[0].occurrence==1U && nodes[1].occurrence==2U);
    assert(nodes[1].world_transform[9]==30.0f);
    assert(nodes[0].owner==((uint64_t(thing)<<32)|23U));

    // Stale/disagreeing native matrix state must clear partial output.
    auto bad_refresh=refresh;
    put(pose,float(77)); // First component mismatch.
    assert(!collect_planar_reflection_native_nodes(
        read,source,bad_refresh,nodes));
    assert(nodes.empty());
    put(pose,identity[0]);

    // Invalid parent cycle is rejected even within an amputated branch.
    put(hierarchy+0xb4U+0x50U,uint32_t(hierarchy+2U*0xb4U));
    assert(!collect_planar_reflection_native_nodes(
        read,source,refresh,nodes));
    assert(nodes.empty());
    put(hierarchy+0xb4U+0x50U,uint32_t(hierarchy));

    // A Thing recycled with a new signature/model cannot yield old nodes.
    source.signature=24;
    assert(!collect_planar_reflection_native_nodes(
        read,source,refresh,nodes));
    source.signature=23;
    put(rd+0x30U,int32_t(3));
    assert(!collect_planar_reflection_native_nodes(
        read,source,refresh,nodes));
    assert(nodes.empty());
    return 0;
}
