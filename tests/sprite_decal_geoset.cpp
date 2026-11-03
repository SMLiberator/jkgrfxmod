#include "renderer/sprite_decal_geoset.hpp"
#include <array>
#include <cassert>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <vector>

int main() {
    using namespace jkgm;
    constexpr uint32_t model=0x20000U,thing=0x24000U;
    constexpr uint32_t nodes=0x30000U,poses=0x40000U;
    constexpr uint32_t low=0x50000U,active=0x60000U,high=0x70000U;
    std::vector<uint8_t> mem(0x90000U);
    auto set=[&](uintptr_t at,auto const &value) {
        assert(at+sizeof(value)<=mem.size());
        std::memcpy(mem.data()+at,&value,sizeof(value));
    };
    auto read=[&](uintptr_t at,void* out,size_t n) {
        if(at>mem.size() || n>mem.size()-at)return false;
        std::memcpy(out,mem.data()+at,n);return true;
    };
    set(thing+4U,model);
    set(thing+0x20U,poses);
    int32_t forced=1;
    set(thing+0x30U,forced);
    uint32_t num_geos=3U,num_nodes=3U,default_geo=0U;
    set(model+0x44U,num_geos);
    set(model+0x50U,default_geo);
    set(model+0x54U,num_nodes);
    set(model+0x58U,nodes);
    set(model+0x24U,2U);set(model+0x28U,low);
    set(model+0x2cU,2U);set(model+0x30U,active);
    set(model+0x34U,3U);set(model+0x38U,high);
    // Higher-resolution GeoSet has an additional arm/leg node.
    std::array<gpu_3do_matrix34,3> pose{};
    for(uint32_t i=0;i<3;++i) {
        pose[i]={{1,0,0,0,1,0,0,0,1,
                  float(i),float(i*2U),float(i*3U)}};
        set(poses+48U*i,pose[i]);
        set(nodes+0xb4U*i+0x44U,i);
        set(nodes+0xb4U*i+0x4cU,i);
    }
    auto candidates=enumerate_sprite_decal_lod_meshes(
        read,thing,model,active+0x70U,pose[1]);
    assert(candidates.size()==5U);
    assert(candidates[0].geoset==0U && candidates[0].mesh==low);
    assert(candidates[1].geoset==0U &&
           candidates[1].mesh==low+0x70U);
    assert(candidates[2].geoset==2U && candidates[2].mesh==high);
    assert(candidates[4].geoset==2U &&
           candidates[4].node==2U &&
           candidates[4].mesh==high+2U*0x70U);
    assert(candidates[4].transform==pose[2]);
    gpu_3do_matrix34 current{};
    assert(read_sprite_decal_lod_node_pose(read,thing,2U,current));
    assert(current==pose[2]);
    assert(decal_geoset_same_pose(current,pose[2]));
    current[9]+=0.01f;
    assert(!decal_geoset_same_pose(current,pose[2]));
    // The default GeoSet is used when the Thing has no explicit override.
    forced=-1;set(thing+0x30U,forced);
    default_geo=1U;set(model+0x50U,default_geo);
    assert(enumerate_sprite_decal_lod_meshes(
        read,thing,model,active+0x70U,pose[1]).size()==5U);
    // Wrong active LOD/mesh/node placement MUST fail closed.
    default_geo=0U;set(model+0x50U,default_geo);
    assert(enumerate_sprite_decal_lod_meshes(
        read,thing,model,active+0x70U,pose[1]).empty());
    default_geo=1U;set(model+0x50U,default_geo);
    current=pose[1];current[10]+=0.1f;
    assert(enumerate_sprite_decal_lod_meshes(
        read,thing,model,active+0x70U,current).empty());
    set(thing+4U,0U);
    assert(enumerate_sprite_decal_lod_meshes(
        read,thing,model,active+0x70U,pose[1]).empty());
}
