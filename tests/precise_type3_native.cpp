#include "renderer/precise_type3_native.hpp"
#include <cassert>
#include <cmath>
#include <cstring>
#include <vector>
using namespace jkgm;

int main()
{
    constexpr uintptr_t base=0x10000000U;
    constexpr uintptr_t thing=base+0x100;
    constexpr uintptr_t model=base+0x800;
    constexpr uintptr_t nodes=base+0x1000;
    constexpr uintptr_t meshes=base+0x2000;
    constexpr uintptr_t vertices=base+0x3000;
    constexpr uintptr_t faces=base+0x4000;
    constexpr uintptr_t indices=base+0x5000;
    std::vector<uint8_t> bytes(0x10000,0);
    auto set=[&](uintptr_t at,void const *source,size_t size) {
        assert(at>=base && size<=bytes.size()-(at-base));
        std::memcpy(bytes.data()+at-base,source,size);
    };
    auto u32=[&](uintptr_t at,uint32_t v) {set(at,&v,4);};
    auto xyz=[&](uintptr_t at,precise_type3::vec3 v) {
        set(at,v.data(),sizeof(v));
    };
    u32(thing+0x0c,10); // SithThing type
    u32(thing+0x74,22); // Thing signature
    u32(thing+0xc4,1); // RD_THING_MODEL3
    u32(thing+0xc8,uint32_t(model));
    u32(model+0x44,1); // one geoset
    u32(model+0x54,1); // one hierarchy node
    u32(model+0x58,uint32_t(nodes));
    u32(model+0x24,1); // geoset 0 has one mesh
    u32(model+0x28,uint32_t(meshes));
    u32(nodes+0x44,0);
    u32(nodes+0x4c,0); // mesh index 0
    u32(nodes+0x50,0); // root
    xyz(nodes+0x60,{{0,0,0}});
    xyz(nodes+0x6c,{{10,5,0}}); // authored node offset
    u32(meshes+0x30,uint32_t(vertices));
    u32(meshes+0x48,4); // four local vertices
    float radius=2.0f;
    set(meshes+0x54,&radius,4);
    u32(meshes+0x40,uint32_t(faces));
    u32(meshes+0x50,1);
    xyz(vertices+0, {{0,0,0}});
    xyz(vertices+12,{{1,0,0}});
    xyz(vertices+24,{{1,1,0}});
    xyz(vertices+36,{{0,1,0}});
    u32(faces+0x14,4);
    u32(faces+0x18,uint32_t(indices));
    xyz(faces+0x34,{{0,0,1}});
    uint32_t const index[4]={0,1,2,3};
    set(indices,index,sizeof(index));

    int reads=0;
    thing_node_reader const reader=[&](uintptr_t addr,void *dst,size_t n) {
        ++reads;
        if(addr==jkgm_node_matrix_address &&
           n==jkgm_node_matrix_prologue.size()) {
            std::memcpy(dst,jkgm_node_matrix_prologue.data(),n);
            return true;
        }
        if(addr<base || addr-base>bytes.size() ||
           n>bytes.size()-(addr-base))return false;
        std::memcpy(dst,bytes.data()+addr-base,n);
        return true;
    };
    // Parent node position is baked into the returned WORLD node matrix
    // by rdModel3_GetMeshMatrix. The mesh vertices remain local.
    gpu_3do_matrix34 pose_matrix{{
        1,0,0, 0,1,0, 0,0,1, 10,5,0
    }};
    mots_native_pose_reader const pose=[&](
        uint32_t node,gpu_3do_matrix34 &out) {
        assert(node==0);
        out=pose_matrix;
        return true;
    };
    precise_type3_native_cache cache{};
    precise_type3_hit hit{};
    precise_type3_stats stats{};
    auto cast=[&](precise_type3::vec3 start,
                  precise_type3::vec3 direction,
                  float length=20.0f,uint64_t revision=1,
                  uint64_t now=1000) {
        return cache.cast(reader,thing,base,revision,true,now,
            pose,start,direction,length,hit,&stats);
    };
    assert(cast({{10.5f,5.5f,6}},{{0,0,-1}})==precise_type3_status::hit);
    assert(std::fabs(hit.distance-6.0f)<0.0001f);
    assert(hit.mesh==meshes && hit.face==faces);
    assert(stats.rebuilt && stats.nodes_in_bounds==1 &&
           stats.polygon_tests==1);
    int const before=reads;
    assert(cast({{10.5f,5.5f,6}},{{0,0,-1}},20,1,1001)==
           precise_type3_status::hit);
    assert(!stats.rebuilt && reads-before<40);

    // Miss the transformed mesh: do not accidentally hit the same square
    // at its untransformed (world-origin) location.
    assert(cast({{0.5f,0.5f,6}},{{0,0,-1}})==
           precise_type3_status::miss);
    assert(stats.polygon_tests==0 && stats.nodes_in_bounds==0);

    // Rotate the authored-offset node about world Z. Its square is now
    // x=[9,10], y=[5,6] and must still raycast with world-space distance.
    pose_matrix={{0,1,0, -1,0,0, 0,0,1, 10,5,0}};
    assert(cast({{9.5f,5.5f,6}},{{0,0,-1}})==
           precise_type3_status::hit);
    assert(std::fabs(hit.distance-6.0f)<0.0001f);
    assert(cast({{10.5f,5.5f,6}},{{0,0,-1}})==
           precise_type3_status::miss);

    // A geometry revision must replace cached vertices immediately.
    pose_matrix={{1,0,0, 0,1,0, 0,0,1, 10,5,0}};
    xyz(vertices+0,{{4,0,0}});
    xyz(vertices+12,{{5,0,0}});
    xyz(vertices+24,{{5,1,0}});
    xyz(vertices+36,{{4,1,0}});
    assert(cast({{14.5f,5.5f,6}},{{0,0,-1}},20,2,1002)==
           precise_type3_status::hit);
    assert(stats.rebuilt && hit.face==faces);
    assert(cast({{10.5f,5.5f,6}},{{0,0,-1}},20,2,1003)==
           precise_type3_status::miss);
    return 0;
}
