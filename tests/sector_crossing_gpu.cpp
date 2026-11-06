#include "renderer/sector_crossing_gpu.hpp"
#include <cassert>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <vector>

int main()
{
    using namespace jkgm;
    auto geo=std::make_shared<gpu_3do_geometry>();
    gpu_3do_group group{};
    group.count=6;
    group.bounds_min={{-1,-1,-1}};
    group.bounds_max={{1,1,1}};
    geo->groups.push_back(group);
    gpu_3do_instance instance{};
    instance.geometry=geo;
    instance.transform={{1,0,0,0,1,0,0,0,1,0,0,0}};
    instance.ambient=0.3f;
    instance.colormap_tint={{0.5f,0.6f,0.7f}};
    instance.sector_tint={{0.1f,0.2f,0.3f}};
    instance.sector_tint_valid=true;
    instance.owning_sector_valid=true;
    instance.owning_sector_id=11;
    sector_crossing_vec3 center{};
    float radius=0.0f;
    assert(sector_crossing_instance_sphere(instance,center,radius));
    assert(center[0]==0.0f && std::abs(radius-std::sqrt(3.0f))<0.0001f);
    // Non-normalized RLU must expand the conservative sphere correctly.
    instance.transform[0]=3.0f;
    assert(sector_crossing_instance_sphere(instance,center,radius));
    // Tight corner sphere after X scale=3: sqrt(3^2+1^2+1^2).
    assert(std::abs(radius-std::sqrt(11.0f))<0.0001f);
    instance.transform[0]=1.0f;

    sector_crossing_snapshot snapshot{};
    snapshot.world=0x10000U;
    for(int i=0;i<3;++i) {
        sector_crossing_region r{};
        r.id=i==0?11U:(i==1?55U:99U);
        r.native_sector=0x20000U+i*0x98U;
        r.valid=true;r.adjacency_valid=true;
        r.bounds_min={{i==0?-10.0f:0.0f,-5,-5}};
        r.bounds_max={{i==0?0.0f:10.0f,5,5}};
        r.planes.push_back({{{i==0?-1.0f:1.0f,0,0}},0});
        if(i==0)r.adjoining.push_back(1);
        if(i==1)r.adjoining.push_back(0);
        snapshot.index_by_id.emplace(r.id,i);
        snapshot.sectors.push_back(r);
    }
    // Unconnected third sector AABB overlaps but never participates.
    std::vector<sector_crossing_plane_range> ranges{{0,1},{1,1},{2,1}};
    std::vector<uint8_t> memory(0x50000U,0);
    auto setFloat=[&](size_t address,float value) {
        std::memcpy(memory.data()+address,&value,4);
    };
    auto setU32=[&](size_t address,uint32_t value) {
        std::memcpy(memory.data()+address,&value,4);
    };
    auto read=[&](uintptr_t addr,void *out,size_t size)->bool {
        if(addr>memory.size() || size>memory.size()-addr)return false;
        std::memcpy(out,memory.data()+addr,size);
        return true;
    };
    for(int i=0;i<3;++i) {
        auto p=size_t(snapshot.sectors[i].native_sector);
        setFloat(p+0x04,i==0?0.1f:0.6f);
        setFloat(p+0x08,0.1f);
        setU32(p+0x0c,0x30000U+i*0x400U);
        setFloat(p+0x10,0.25f);
        setFloat(p+0x14,0.35f);
        setFloat(p+0x18,0.45f);
        auto c=0x30000U+i*0x400U+jk_cmp_tint_offset;
        setFloat(c,1.1f);
        setFloat(c+4,0.9f);
        setFloat(c+8,0.7f);
    }
    auto state=sector_crossing_stage_instance(
        snapshot,ranges,instance,read,true);
    assert(state && state->count==2);
    assert(std::abs(state->sectors[0].ambient-0.3f)<0.0001f);
    assert(std::abs(state->sectors[1].ambient-0.8f)<0.0001f);
    assert(state->sectors[0].cmp_tint[0]==0.5f);
    assert(std::abs(state->sectors[1].cmp_tint[0]-1.1f)<0.0001f);
    assert(std::abs(state->sectors[1].sector_tint[0]-0.25f)<0.0001f);
    assert(state->sectors[1].plane_start==1U);
    assert(state->sectors[1].plane_count==1U);
    // Native rdCache skips CMP tint for the identity colormap. Do not
    // globally reinterpret zero as white: non-identity zero means black.
    constexpr uint32_t neighbor_cmp=0x30400U;
    setFloat(neighbor_cmp+jk_cmp_tint_offset,0.0f);
    setFloat(neighbor_cmp+jk_cmp_tint_offset+4U,0.0f);
    setFloat(neighbor_cmp+jk_cmp_tint_offset+8U,0.0f);
    auto identity=sector_crossing_stage_instance(
        snapshot,ranges,instance,read,true,neighbor_cmp);
    assert(identity && identity->sectors[1].cmp_tint[0]==1.0f &&
           identity->sectors[1].cmp_tint[2]==1.0f);
    auto nonidentity=sector_crossing_stage_instance(
        snapshot,ranges,instance,read,true,0x30000U);
    assert(nonidentity && nonidentity->sectors[1].cmp_tint[0]==0.0f);
    setFloat(neighbor_cmp+jk_cmp_tint_offset,1.1f);
    setFloat(neighbor_cmp+jk_cmp_tint_offset+4U,0.9f);
    setFloat(neighbor_cmp+jk_cmp_tint_offset+8U,0.7f);
    // A submesh wholly in the neighbor's sector must still receive
    // different lighting even though the owning Thing lives in sector 11.
    instance.transform[9]=4.0f;
    auto remote=sector_crossing_stage_instance(
        snapshot,ranges,instance,read,true);
    assert(remote && remote->count==2);
    assert(remote->sectors[1].ambient>remote->sectors[0].ambient);
    instance.transform[9]=0.0f;
    auto noTint=sector_crossing_stage_instance(
        snapshot,ranges,instance,read,false);
    assert(noTint && noTint->sectors[1].sector_tint[0]==0.0f);
    // Four candidates max and 96 planes per region. Invalid native
    // geometry never produces partial per-Thing shader data.
    ranges[1].count=97;
    assert(!sector_crossing_stage_instance(
        snapshot,ranges,instance,read,true));
    ranges[1].count=1;
    instance.owning_sector_valid=false;
    assert(!sector_crossing_stage_instance(
        snapshot,ranges,instance,read,true));
    instance.owning_sector_valid=true;
    // Skybox ThingFlag 0x20 must bypass sector-crossing color and
    // lighting even when the feature is enabled and sectors overlap.
    instance.skybox_vertex_lighting=true;
    assert(!sector_crossing_stage_instance(
        snapshot,ranges,instance,read,true));
    instance.skybox_vertex_lighting=false;
    instance.camera_hud=true;
    assert(!sector_crossing_stage_instance(
        snapshot,ranges,instance,read,true));
    return 0;
}
