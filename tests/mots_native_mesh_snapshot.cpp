#include "renderer/mots_native_collision_query.hpp"
#include <cassert>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <limits>
#include <vector>
using namespace jkgm;

static bool near(float a,float b) {return std::fabs(a-b)<0.001f;}
int main()
{
    constexpr uintptr_t base=0x10000000U;
    constexpr uintptr_t thing=base+0x100;
    constexpr uintptr_t model=base+0x800;
    constexpr uintptr_t nodes=base+0x1400;
    constexpr uintptr_t meshes=base+0x2800;
    constexpr uintptr_t amputation=base+0x3400;
    constexpr uintptr_t verts0=base+0x4000;
    constexpr uintptr_t verts1=base+0x4600;
    std::vector<uint8_t> mem(0x60000,0);
    auto set_u32=[&](uintptr_t at,uint32_t value) {
        assert(at>=base && (at-base)+4<=mem.size());
        std::memcpy(mem.data()+(at-base),&value,4);
    };
    auto set_f32=[&](uintptr_t at,float value) {
        assert(at>=base && (at-base)+4<=mem.size());
        std::memcpy(mem.data()+(at-base),&value,4);
    };
    auto set_vec=[&](uintptr_t at,thing_node_vector const &v) {
        assert(at>=base && (at-base)+sizeof(v)<=mem.size());
        std::memcpy(mem.data()+(at-base),v.data(),sizeof(v));
    };
    set_u32(thing+0x0c,10);                  // valid SithThing
    set_u32(thing+0xc4,1);                   // RD_THING_MODEL3
    set_u32(thing+0xc8,uint32_t(model));
    set_u32(thing+0xc4+0x28,uint32_t(amputation));
    set_u32(thing+0xc4+0x30,1);              // explicitly select geoset 1
    set_u32(model+0x44,2);                   // number of geosets
    set_u32(model+0x50,0);                   // default geoset 0
    set_u32(model+0x54,3);                   // node count
    set_u32(model+0x58,uint32_t(nodes));
    set_u32(model+0x24+8,2);                // geoset 1 has two meshes
    set_u32(model+0x24+12,uint32_t(meshes));
    const float placement[12]{1,0,0, 0,1,0, 0,0,1, 0,0,0};
    std::memcpy(mem.data()+(thing+0x88-base),
                placement,sizeof(placement));
    auto set_node=[&](int i,int parent,int mesh,thing_node_vector pivot) {
        uintptr_t at=nodes+uintptr_t(i)*0xb4U;
        set_u32(at+0x40,0); // node flags
        set_u32(at+0x44,uint32_t(i));
        set_u32(at+0x4c,uint32_t(mesh));
        set_u32(at+0x50,parent<0?0U:uint32_t(nodes+uintptr_t(parent)*0xb4U));
        set_vec(at+0x60,pivot);
    };
    set_node(0,-1,-1,{{0,0,0}});            // empty root
    set_node(1,0,0,{{0,1,0}});              // knee pivot, first mesh
    set_node(2,1,1,{{0,0,2}});              // child mesh
    set_u32(meshes+0x30,uint32_t(verts0));
    set_u32(meshes+0x48,4);
    set_f32(meshes+0x54,4.1f);
    set_vec(verts0+0,{{-0.25f,0,0}});
    set_vec(verts0+12,{{0.25f,0,0}});
    set_vec(verts0+24,{{-0.25f,-4,0}});
    set_vec(verts0+36,{{0.25f,-4,0}});
    set_u32(meshes+0x70+0x30,uint32_t(verts1));
    set_u32(meshes+0x70+0x48,3);
    set_f32(meshes+0x70+0x54,1.0f);
    set_vec(verts1+0,{{0,0,0}});
    set_vec(verts1+12,{{1,0,0}});
    set_vec(verts1+24,{{0,1,0}});
    int reads=0;
    bool supported_build=true;
    thing_node_reader reader=[&](uintptr_t at,void *dst,size_t length) {
        ++reads;
        if(at==jkgm_node_matrix_address &&
           length==jkgm_node_matrix_prologue.size()) {
            auto bytes=jkgm_node_matrix_prologue;
            if(!supported_build)bytes[0]=0x90;
            std::memcpy(dst,bytes.data(),bytes.size());
            return true;
        }
        if(at<base || at-base>mem.size() ||
           length>mem.size()-(at-base))return false;
        std::memcpy(dst,mem.data()+(at-base),length);
        return true;
    };
    mots_native_model_snapshot source{};
    assert(read_native_collision_model(reader,thing,source));
    assert(source.model==model && source.geoset==1 && source.nodes.size()==3);
    assert(source.nodes[0].mesh_index==-1);
    assert(source.nodes[1].parent==0 && source.nodes[2].parent==1);
    assert(source.nodes[1].mesh_index==0);
    assert(source.nodes[1].local_vertices.size()==4);
    assert(near(source.nodes[1].authored_radius,4.1f));
    assert(near(source.nodes[1].fitted_sphere.center[1],-2));
    assert(source.nodes[1].fitted_sphere.radius<4.1f);
    assert(near(source.nodes[1].pivot[1],1));
    assert(reads>10);
    supported_build=false;
    assert(!read_native_collision_model(reader,thing,source));
    supported_build=true;
    // rdThing geoset 0xffffffff uses rdModel3 default selection.
    set_u32(thing+0xc4+0x30,0xffffffffU);
    set_u32(model+0x50,1);
    assert(read_native_collision_model(reader,thing,source));
    assert(source.geoset==1);

    // Pose provider is deliberately separate from the memory reader.
    // It stands in for the existing signature-checked native accessor.
    int calls=0;
    mots_native_pose_reader pose=[&](uint32_t index,gpu_3do_matrix34 &out) {
        ++calls;
        out={{1,0,0, 0,1,0, 0,0,1, 10.0f+index,20,30}};
        return true;
    };
    std::vector<mots_tree_node> tree{};
    assert(build_native_collision_tree(source,pose,true,tree));
    assert(tree.size()==3 && calls==2); // empty root does not request pose
    assert(tree[1].center_on_geometry);
    assert(tree[1].parent==0 && tree[2].parent==1);
    thing_node_vector center{};
    float radius=0;
    assert(mots_tree_world_sphere(tree[1],center,radius));
    assert(near(center[0],11) && near(center[1],18));
    assert(near(radius,source.nodes[1].fitted_sphere.radius));

    // Full read-only pipeline: synthetic JKGM bytes -> validated 3DO
    // snapshot -> animated matrices -> nearest collision sphere.
    mots_collision_query_hit collision{};
    mots_collision_query_stats metrics{};
    assert(mots_probe_collision_tree(
        source,pose,true,{{10,20,30}},{{11,18,40}},{{0,0,-1}},
        20.0f,0.5f,collision,&metrics)==mots_query_status::hit);
    assert(collision.node==1);
    assert(near(collision.distance,
                10.0f-source.nodes[1].fitted_sphere.radius-0.5f));
    assert(metrics.active_meshes==2 && metrics.tested_broadphase);

    // Legacy path uses the authored origin-centered radius * 0.75.
    assert(build_native_collision_tree(source,pose,false,tree));
    assert(!tree[1].center_on_geometry);
    assert(mots_tree_world_sphere(tree[1],center,radius));
    assert(near(center[1],20) && near(radius,4.1f*0.75f));

    // Parent amputation must suppress descendants, regardless of their
    // draw-order position in the hierarchy array.
    set_u32(amputation+4,1);
    assert(read_native_collision_model(reader,thing,source));
    calls=0;
    assert(build_native_collision_tree(source,pose,true,tree));
    assert(tree[1].amputated && !tree[2].amputated);
    assert(calls==0);
    set_u32(amputation+4,0);

    // Parent nodes are not guaranteed to appear before children. Populate
    // all links and amputations before requesting even one live pose.
    auto reverse_parent_order=source;
    reverse_parent_order.nodes[1].amputated=false;
    reverse_parent_order.nodes[1].parent=2;
    reverse_parent_order.nodes[2].parent=-1;
    reverse_parent_order.nodes[2].amputated=true;
    calls=0;
    assert(build_native_collision_tree(reverse_parent_order,pose,true,tree));
    assert(calls==0);

    // Changes during a failed source read must not partially publish data.
    auto before=source;
    set_u32(nodes+0xb4+0x50,uint32_t(nodes+1)); // misaligned parent
    assert(!read_native_collision_model(reader,thing,source));
    assert(source.nodes[1].parent==before.nodes[1].parent);
    set_u32(nodes+0xb4+0x50,uint32_t(nodes));
    set_u32(nodes+0xb4+0x4c,99); // invalid mesh index
    assert(!read_native_collision_model(reader,thing,source));
    set_u32(nodes+0xb4+0x4c,0);
    set_f32(verts0+24,std::numeric_limits<float>::quiet_NaN());
    assert(!read_native_collision_model(reader,thing,source));
    set_vec(verts0+24,{{-0.25f,-4,0}});

    // More than 64 KiB of vertices must read in bounded chunks.
    constexpr uint32_t many=6000;
    constexpr uintptr_t big_verts=base+0x8000;
    for(uint32_t i=0;i<many;++i)
        set_vec(big_verts+uintptr_t(i)*12,{{float(i%11),0,0}});
    set_u32(meshes+0x30,uint32_t(big_verts));
    set_u32(meshes+0x48,many);
    size_t largest=0;
    thing_node_reader limited=[&](uintptr_t at,void *dst,size_t length) {
        largest=std::max(largest,length);
        if(at==jkgm_node_matrix_address &&
           length==jkgm_node_matrix_prologue.size()) {
            std::memcpy(dst,jkgm_node_matrix_prologue.data(),length);
            return true;
        }
        if(length>0x10000 || at<base || at-base>mem.size() ||
           length>mem.size()-(at-base))return false;
        std::memcpy(dst,mem.data()+(at-base),length);
        return true;
    };
    assert(read_native_collision_model(limited,thing,source));
    assert(source.nodes[1].local_vertices.size()==many);
    assert(largest<=0x10000);
}
