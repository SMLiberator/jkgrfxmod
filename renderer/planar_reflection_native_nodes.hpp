#pragma once
// Camera-independent native model/pose source for planar reflection captures.
// Read-only except for the caller-supplied engine pose-refresh callback:
// rdModel3_GetMeshMatrix refreshes a Thing's own animation cache, not rdCamera.
// No rdModel3_Draw, no native render traversal, no OpenGL.
#include "planar_reflection_things.hpp"
#include "precise_type3_pose_bulk.hpp"
#include "sprite_decal_geoset.hpp"
#include <array>
#include <cstdint>
#include <unordered_map>
#include <vector>

namespace jkgm {
    struct planar_reflection_native_node {
        uintptr_t thing=0,model=0,mesh=0;
        uint32_t geoset=0,hierarchy_index=0,occurrence=0;
        uint64_t owner=0;
        gpu_3do_matrix34 world_transform{};
    };

    // Source pointer and signature are rechecked after discovery: the game
    // can recycle a Thing or switch its model/GeoSet before capture.
    // Failed reads and invalid parent trees never emit partial geometry.
    template<class Read,class Refresh>
    bool collect_planar_reflection_native_nodes(
        Read const &read,planar_reflection_model_thing const &source,
        Refresh const &refresh,std::vector<planar_reflection_native_node> &out)
    {
        out.clear();
        using namespace mots_native_layout;
        if(!thing_node_valid_address(source.thing,0x10cU) ||
           !thing_node_valid_address(source.model,0x5cU) ||
           !validate_native_thing_node_accessor(read))
            return false;
        thing_node_native_input current{};
        if(!resolve_native_thing_node(read,source.thing,0,current) ||
           source.model==0U)return false;

        uint32_t signature=0,linked_model=0,linked_owner=0;
        if(!read(source.thing+0x74U,&signature,4) ||
           !read(current.rd_thing+4U,&linked_model,4) ||
           !read(current.rd_thing+0x44U,&linked_owner,4) ||
           signature!=source.signature ||
           linked_model!=source.model ||
           linked_owner!=source.thing)return false;

        uint32_t count=0,hierarchy=0,meshes_count=0,meshes=0,amputations=0;
        if(!read(source.model+model_num_nodes,&count,4) ||
           !read(source.model+model_hierarchy,&hierarchy,4) ||
           count==0U || count>max_nodes ||
           !thing_node_valid_address(hierarchy,node_stride) ||
           !read(current.rd_thing+rd_amputations,&amputations,4))
            return false;
        uint32_t const geo=read_sprite_decal_active_geoset(
            read,current.rd_thing,source.model);
        if(geo==0xffffffffU ||
           !read(source.model+model_geosets+
                 uintptr_t(geo)*geoset_stride,&meshes_count,4) ||
           !read(source.model+model_geosets+
                 uintptr_t(geo)*geoset_stride+4,&meshes,4) ||
           meshes_count>max_meshes ||
           (meshes_count && !thing_node_valid_address(meshes,mesh_stride)))
            return false;

        struct node {
            int32_t parent=-1;
            uint32_t mesh=0xffffffffU;
            bool amputated=false;
        };
        std::vector<node> hierarchy_nodes(count);
        for(uint32_t i=0;i<count;++i) {
            uintptr_t const at=uintptr_t(hierarchy)+uintptr_t(i)*node_stride;
            uint32_t id=0,mesh=0,parent=0;
            if(!thing_node_valid_address(at,0x54U) ||
               !read(at+node_index,&id,4) ||
               !read(at+node_mesh,&mesh,4) ||
               !read(at+node_parent,&parent,4) ||
               id!=i ||
               !mots_native_detail::hierarchy_pointer_to_index(
                   parent,hierarchy,count,hierarchy_nodes[i].parent) ||
               hierarchy_nodes[i].parent==int32_t(i))
                return false;
            hierarchy_nodes[i].mesh=mesh;
            if(amputations) {
                uint32_t flag=0;
                uintptr_t const cut=uintptr_t(amputations)+uintptr_t(i)*4U;
                if(!thing_node_valid_address(cut,4U) ||
                   !read(cut,&flag,4U))return false;
                hierarchy_nodes[i].amputated=flag!=0U;
            }
        }
        // Validate ALL parent chains, including empty and amputated nodes.
        // Otherwise a malformed cycle might escape detection if every mesh
        // within that branch is hidden or belongs only to a different LOD.
        std::vector<uint8_t> active(count,0xffU);
        for(size_t i=0;i<count;++i) {
            int32_t p=int32_t(i);
            unsigned depth=0;
            bool visible=true;
            while(p>=0) {
                if(++depth>count)return false;
                auto const &node=hierarchy_nodes[size_t(p)];
                if(node.amputated)visible=false;
                p=node.parent;
            }
            active[i]=visible?1U:0U;
        }

        // Refresh once, then read the complete CURRENT pose buffer as used
        // by rdModel3_DrawHNode. Do not invent root-only transforms.
        std::vector<gpu_3do_matrix34> poses;
        if(!precise_type3_read_pose_batch(
               read,current.rd_thing,count,
               [&](gpu_3do_matrix34 &first)->bool {
                   return refresh(current.rd_thing,current.world_placement,
                                  first);
               },poses))
            return false;
        std::unordered_map<uintptr_t,uint32_t> occurrences;
        std::vector<planar_reflection_native_node> found;
        found.reserve(count);
        for(uint32_t i=0;i<count;++i) {
            auto const &node=hierarchy_nodes[i];
            if(!active[i] || node.mesh==0xffffffffU ||
               node.mesh>=meshes_count)continue;
            if(!gpu_3do_matrix_finite(poses[i]))return false;
            uintptr_t const mesh=uintptr_t(meshes)+
                                  uintptr_t(node.mesh)*mesh_stride;
            if(!thing_node_valid_address(mesh,sizeof(uint32_t)))return false;
            planar_reflection_native_node entry{};
            entry.thing=source.thing;
            entry.model=source.model;
            entry.mesh=mesh;
            entry.geoset=geo;
            entry.hierarchy_index=i;
            entry.occurrence=++occurrences[mesh];
            entry.owner=(uint64_t(uint32_t(source.thing))<<32)|
                         source.signature;
            entry.world_transform=poses[i];
            found.push_back(entry);
        }
        out=std::move(found);
        return true;
    }
} // namespace jkgm
