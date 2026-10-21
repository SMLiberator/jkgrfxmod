#pragma once
// Read-only snapshot of JKGM's 3DO collision geometry and hierarchy.
//
// Layouts refer to the supplied patched x86 jkgm.exe, not MotS. The only
// engine access is through a caller-supplied checked memory reader.
// In particular, this header NEVER calls rdModel3_GetMeshMatrix; a separate
// pose provider may be used after a successful immutable snapshot.
#include "mots_mesh_collision_bounds.hpp"
#include "mots_sphere_tree.hpp"
#include "thing_node_native.hpp"
#include <algorithm>
#include <array>
#include <cstdint>
#include <cstring>
#include <functional>
#include <limits>
#include <utility>
#include <vector>

namespace jkgm {
    namespace mots_native_layout {
        constexpr uintptr_t model_num_geos=0x44;
        constexpr uintptr_t model_default_geoset=0x50;
        constexpr uintptr_t model_num_nodes=0x54;
        constexpr uintptr_t model_hierarchy=0x58;
        constexpr uintptr_t model_geosets=0x24;
        constexpr uintptr_t geoset_stride=8;
        constexpr uintptr_t mesh_stride=0x70;
        constexpr uintptr_t mesh_vertices=0x30;
        constexpr uintptr_t mesh_vertex_count=0x48;
        constexpr uintptr_t mesh_radius=0x54;
        constexpr uintptr_t node_stride=0xB4;
        constexpr uintptr_t node_flags=0x40;
        constexpr uintptr_t node_index=0x44;
        constexpr uintptr_t node_mesh=0x4C;
        constexpr uintptr_t node_parent=0x50;
        constexpr uintptr_t node_pivot=0x60;
        constexpr uintptr_t rd_geoset_select=0x30;
        constexpr uintptr_t rd_amputations=0x28;
        constexpr uint32_t max_nodes=4096;
        constexpr uint32_t max_meshes=4096;
        constexpr uint32_t max_vertices_per_mesh=65536;
        constexpr uint32_t max_total_vertices=262144;
        constexpr size_t max_read_chunk=0x10000;
    }

    struct mots_native_mesh_snapshot {
        uint32_t hierarchy_index=0;
        int32_t parent=-1;
        int32_t mesh_index=-1;
        uint32_t flags=0;
        bool amputated=false;
        thing_node_vector pivot{};
        float authored_radius=0.0f;
        // Never alter 3DO vertex coordinates or hierarchy pivot here.
        std::vector<thing_node_vector> local_vertices;
        mots_local_mesh_sphere fitted_sphere{};
        bool has_fitted_sphere=false;
    };
    struct mots_native_model_snapshot {
        uintptr_t model=0;
        uint32_t geoset=0;
        std::vector<mots_native_mesh_snapshot> nodes;
    };

    namespace mots_native_detail {
        inline bool read_u32(thing_node_reader const &read,uintptr_t at,uint32_t &v) {
            return thing_node_valid_address(at,4) && read(at,&v,4);
        }
        inline bool read_f32(thing_node_reader const &read,uintptr_t at,float &v) {
            return thing_node_valid_address(at,4) && read(at,&v,4);
        }
        inline bool read_vec3(thing_node_reader const &read,uintptr_t at,
                              thing_node_vector &v) {
            return thing_node_valid_address(at,sizeof(v)) &&
                   read(at,v.data(),sizeof(v)) &&
                   std::isfinite(v[0]) && std::isfinite(v[1]) && std::isfinite(v[2]);
        }
        inline bool read_vertex_array(thing_node_reader const &read,
                                      uintptr_t address,uint32_t count,
                                      std::vector<thing_node_vector> &vertices)
        {
            static_assert(sizeof(thing_node_vector)==sizeof(float)*3,
                          "JK stores contiguous float3 vertices");
            if(!count || count>mots_native_layout::max_vertices_per_mesh)
                return false;
            size_t const total=size_t(count)*sizeof(thing_node_vector);
            // No engine read crosses the limit enforced by the existing
            // ThingNode address validation. Large patched models are read
            // in <=64 KiB chunks.
            if(!thing_node_valid_address(address,4) ||
               address>0x80000000U-total)return false;
            std::vector<thing_node_vector> tmp(count);
            uint8_t *const dst=reinterpret_cast<uint8_t *>(tmp.data());
            for(size_t offset=0;offset<total;) {
                size_t const bytes=std::min(
                    total-offset,mots_native_layout::max_read_chunk);
                if(!thing_node_valid_address(address+offset,bytes) ||
                   !read(address+offset,dst+offset,bytes))return false;
                offset+=bytes;
            }
            for(auto const &v:tmp)
                for(float x:v)
                    if(!std::isfinite(x))return false;
            vertices=std::move(tmp);
            return true;
        }
        inline bool hierarchy_pointer_to_index(
            uintptr_t address,uintptr_t base,uint32_t count,int32_t &idx)
        {
            if(address==0) {idx=-1;return true;}
            if(address<base)return false;
            uintptr_t const diff=address-base;
            if(diff%mots_native_layout::node_stride!=0 ||
               diff/mots_native_layout::node_stride>=count)return false;
            idx=static_cast<int32_t>(diff/mots_native_layout::node_stride);
            return true;
        }
    }

    // Snapshots the *selected* geoset and all hierarchy nodes, including
    // empty parent nodes, so amputated branches remain correctly linked.
    // A failed read preserves 'out'. This is suitable for a future cached
    // model-geometry source, but does not install any live collision hook.
    inline bool read_native_collision_model(
        thing_node_reader const &read,uintptr_t thing,
        mots_native_model_snapshot &out)
    {
        using namespace mots_native_layout;
        using namespace mots_native_detail;
        thing_node_native_input validated{};
        // Match the already verified JKGM binary before interpreting any
        // game object as an rdModel3/rdHierarchyNode. Never silently use
        // these offsets against a different executable.
        if(!read || !validate_native_thing_node_accessor(read) ||
           !resolve_native_thing_node(read,thing,0,validated))
            return false;
        uint32_t model_addr=0;
        if(!read_u32(read,validated.rd_thing+4,model_addr))return false;
        uintptr_t const model=model_addr;
        uint32_t geos=0,preferred=0,selection=0,count=0,hierarchy_addr=0;
        if(!read_u32(read,model+model_num_geos,geos) ||
           !read_u32(read,model+model_default_geoset,preferred) ||
           !read_u32(read,model+model_num_nodes,count) ||
           !read_u32(read,model+model_hierarchy,hierarchy_addr) ||
           !read_u32(read,validated.rd_thing+rd_geoset_select,selection) ||
           geos==0 || geos>4 || count==0 || count>max_nodes ||
           !thing_node_valid_address(hierarchy_addr,0x70))
            return false;
        if(selection==0xffffffffU) selection=preferred;
        if(selection>=geos)return false;

        uint32_t mesh_count=0,mesh_addr=0,amputations=0;
        uintptr_t const geo=model+model_geosets+selection*geoset_stride;
        if(!read_u32(read,geo,mesh_count) ||
           !read_u32(read,geo+4,mesh_addr) ||
           !read_u32(read,validated.rd_thing+rd_amputations,amputations) ||
           mesh_count==0 || mesh_count>max_meshes ||
           !thing_node_valid_address(mesh_addr,mesh_stride))
            return false;

        mots_native_model_snapshot snapshot{};
        snapshot.model=model;
        snapshot.geoset=selection;
        snapshot.nodes.reserve(count);
        uint32_t total_vertices=0;
        for(uint32_t i=0;i<count;++i) {
            uintptr_t const node=uintptr_t(hierarchy_addr)+
                                 uintptr_t(i)*node_stride;
            if(!thing_node_valid_address(node,0x6c))return false;
            mots_native_mesh_snapshot entry{};
            uint32_t id=0,mesh_id=0,parent_addr=0,flags=0;
            if(!read_u32(read,node+node_index,id) || id!=i ||
               !read_u32(read,node+node_mesh,mesh_id) ||
               !read_u32(read,node+node_parent,parent_addr) ||
               !read_u32(read,node+node_flags,flags) ||
               !read_vec3(read,node+node_pivot,entry.pivot) ||
               !hierarchy_pointer_to_index(parent_addr,hierarchy_addr,
                                            count,entry.parent) ||
               entry.parent==static_cast<int32_t>(i))
                return false;
            entry.hierarchy_index=i;
            entry.flags=flags;
            if(amputations) {
                uint32_t cut=0;
                if(!read_u32(read,uintptr_t(amputations)+size_t(i)*4,cut))
                    return false;
                entry.amputated=cut!=0;
            }

            if(mesh_id!=0xffffffffU) {
                if(mesh_id>=mesh_count)return false;
                uintptr_t const mesh=uintptr_t(mesh_addr)+
                                     uintptr_t(mesh_id)*mesh_stride;
                uint32_t vertices_addr=0,vertex_count=0;
                float radius=0;
                if(!read_u32(read,mesh+mesh_vertices,vertices_addr) ||
                   !read_u32(read,mesh+mesh_vertex_count,vertex_count) ||
                   !read_f32(read,mesh+mesh_radius,radius) ||
                   !std::isfinite(radius) || radius<0 ||
                   vertex_count>max_vertices_per_mesh ||
                   vertex_count>max_total_vertices-total_vertices)
                    return false;
                total_vertices+=vertex_count;
                entry.mesh_index=static_cast<int32_t>(mesh_id);
                entry.authored_radius=radius;
                if(vertex_count>0) {
                    if(!read_vertex_array(read,vertices_addr,vertex_count,
                                          entry.local_vertices))
                        return false;
                    entry.has_fitted_sphere=mots_center_mesh_sphere(
                        entry.local_vertices,entry.fitted_sphere);
                    if(!entry.has_fitted_sphere)return false;
                }
            }
            snapshot.nodes.push_back(std::move(entry));
        }
        // Reject malformed parent cycles before any pose callback can run.
        std::vector<mots_tree_node> hierarchy(count);
        for(size_t i=0;i<count;++i) {
            hierarchy[i].parent=snapshot.nodes[i].parent;
            hierarchy[i].amputated=false;
        }
        for(size_t i=0;i<count;++i)
            if(!mots_tree_node_active(hierarchy,static_cast<uint32_t>(i)))
                return false;

        out=std::move(snapshot);
        return true;
    }

    // Optional second phase: the caller provides verified current WORLD
    // matrices, e.g. via rdModel3_GetMeshMatrix after validating its
    // signature. The static snapshot above *does not* update animation.
    using mots_native_pose_reader=std::function<
        bool(uint32_t,gpu_3do_matrix34 &)>;
    inline bool build_native_collision_tree(
        mots_native_model_snapshot const &snapshot,
        mots_native_pose_reader const &pose,bool prefer_fitted,
        std::vector<mots_tree_node> &out)
    {
        if(!pose || snapshot.nodes.empty() ||
           snapshot.nodes.size()>mots_native_layout::max_nodes)return false;
        std::vector<mots_tree_node> result(snapshot.nodes.size());
        // Populate all hierarchy links and amputation flags BEFORE walking
        // active nodes; parent indices need not precede their children.
        for(size_t i=0;i<snapshot.nodes.size();++i) {
            result[i].parent=snapshot.nodes[i].parent;
            result[i].amputated=snapshot.nodes[i].amputated;
            result[i].has_mesh=snapshot.nodes[i].mesh_index>=0 &&
                               !snapshot.nodes[i].local_vertices.empty();
        }
        for(size_t i=0;i<snapshot.nodes.size();++i) {
            auto const &src=snapshot.nodes[i];
            auto &dst=result[i];
            if(!dst.has_mesh || !mots_tree_node_active(result,
                              static_cast<uint32_t>(i)))continue;
            dst.center_on_geometry=prefer_fitted && src.has_fitted_sphere;
            if(dst.center_on_geometry) {
                dst.local_center=src.fitted_sphere.center;
                dst.mesh_radius=src.fitted_sphere.radius;
            } else dst.mesh_radius=src.authored_radius;
            if(!pose(static_cast<uint32_t>(i),dst.world_matrix) ||
               !gpu_3do_matrix_finite(dst.world_matrix))
                return false;
        }
        out=std::move(result);
        return true;
    }
}
