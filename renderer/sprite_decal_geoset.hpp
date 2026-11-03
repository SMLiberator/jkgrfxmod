#pragma once
// Enumerate the OTHER LOD GeoSets for a moving 3DO decal receiver, using the
// JKGM binary's rdModel3/rdThing layout, not assumed OpenJKDF2 addresses.
// rdModel3_Draw (0x446060) chooses rdThing+0x30 or model+0x50, then
// passes model+0x24+8*geoset and a rdMesh 0x70*meshIdx to rdModel3_DrawMesh.
// rdModel3_DrawHNode reads the node's index/meshIdx at +0x44/+0x4c and
// the composed (already animated) node matrix from rdThing+0x20.
//
// Every candidate is validated against the CURRENT visible source mesh's
// *exact* composed pose. If any layout assumption is wrong, no variants are
// returned; existing decals continue to render in the impact GeoSet.
#include "gpu_3do_modelview.hpp"
#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <unordered_map>
#include <vector>

namespace jkgm {
struct sprite_decal_geoset_mesh {
    uintptr_t mesh=0;
    uint32_t geoset=0,node=0,occurrence=0;
    gpu_3do_matrix34 transform{};
};
inline bool decal_geoset_ptr(uintptr_t p) {
    return p>=0x10000U && p<=0x7ff00000U;
}
inline bool decal_geoset_same_pose(gpu_3do_matrix34 const &a,
                                   gpu_3do_matrix34 const &b)
{
    for(size_t i=0;i<12U;++i) {
        if(!std::isfinite(a[i]) || !std::isfinite(b[i]) ||
           std::fabs(a[i]-b[i])>0.001f*
               (1.0f+std::max(std::fabs(a[i]),std::fabs(b[i]))))
            return false;
    }
    return true;
}
// Which GeoSet did rdModel3_Draw select for this particular Thing?
// The Thing override wins over rdModel3.geosetSelect; -1 means default.
template<class Read>
uint32_t read_sprite_decal_active_geoset(
    Read const &read,uintptr_t rdthing,uintptr_t model)
{
    if(!decal_geoset_ptr(rdthing) || !decal_geoset_ptr(model))
        return 0xffffffffU;
    int32_t forced=-1;
    uint32_t selected=0,num_geos=0;
    if(!read(rdthing+0x30U,&forced,4) ||
       !read(model+0x50U,&selected,4) ||
       !read(model+0x44U,&num_geos,4) ||
       num_geos==0U || num_geos>4U)
        return 0xffffffffU;
    if(forced!=-1) {
        if(forced<0 || forced>=int32_t(num_geos))
            return 0xffffffffU;
        selected=uint32_t(forced);
    }
    return selected<num_geos?selected:0xffffffffU;
}
// Exact same composed node matrix consumed by rdModel3_DrawHNode.
// Once the actor switches LOD, the rdMesh pointer changes but the skeleton
// node index remains stable. Matching its CURRENT matrix prevents a decal
// on a shared mesh from being replicated onto the wrong animated limb.
template<class Read>
bool read_sprite_decal_lod_node_pose(
    Read const &read,uintptr_t rdthing,uint32_t index,
    gpu_3do_matrix34 &out)
{
    if(!decal_geoset_ptr(rdthing) || index>=256U)return false;
    uint32_t array=0;
    if(!read(rdthing+0x20U,&array,4U) || !decal_geoset_ptr(array))
        return false;
    uintptr_t const at=uintptr_t(array)+uintptr_t(index)*48U;
    return decal_geoset_ptr(at) && read(at,out.data(),48U) &&
           gpu_3do_matrix_finite(out);
}
template<class Read>
std::vector<sprite_decal_geoset_mesh> enumerate_sprite_decal_lod_meshes(
    Read const &read,uintptr_t rdthing,uintptr_t model,
    uintptr_t source_mesh,gpu_3do_matrix34 const &source_world)
{
    std::vector<sprite_decal_geoset_mesh> result;
    if(!decal_geoset_ptr(rdthing) || !decal_geoset_ptr(model) ||
       !decal_geoset_ptr(source_mesh) || !gpu_3do_matrix_finite(source_world))
        return result;
    uint32_t loaded_model=0,pose_array=0,node_array=0;
    uint32_t num_geos=0,num_nodes=0,default_geo=0;
    int32_t forced_geo=-1;
    if(!read(rdthing+0x04U,&loaded_model,4) || loaded_model!=model ||
       !read(rdthing+0x20U,&pose_array,4) ||
       !read(rdthing+0x30U,&forced_geo,4) ||
       !read(model+0x44U,&num_geos,4) ||
       !read(model+0x50U,&default_geo,4) ||
       !read(model+0x54U,&num_nodes,4) ||
       !read(model+0x58U,&node_array,4) ||
       !decal_geoset_ptr(pose_array) || !decal_geoset_ptr(node_array) ||
       num_geos<2U || num_geos>4U ||
       num_nodes==0U || num_nodes>256U)
        return result;
    int32_t const current=forced_geo==-1?int32_t(default_geo):forced_geo;
    if(current<0 || current>=int32_t(num_geos))return result;
    struct geoset {uint32_t count=0,base=0;};
    std::array<geoset,4> sets{};
    for(uint32_t g=0;g<num_geos;++g) {
        if(!read(model+0x24U+g*8U,&sets[g],sizeof(geoset)) ||
           sets[g].count>2048U ||
           (sets[g].count && !decal_geoset_ptr(sets[g].base)))
            return {};
    }
    if(!sets[uint32_t(current)].count)return result;
    struct node {
        uint32_t index=0,mesh_index=0;
        gpu_3do_matrix34 world{};
        bool valid=false;
    };
    std::vector<node> nodes;
    nodes.reserve(num_nodes);
    bool source_confirmed=false;
    for(uint32_t i=0;i<num_nodes;++i) {
        uint32_t id=0,mesh_index=0;
        uintptr_t const offset=uintptr_t(node_array)+uintptr_t(i)*0xb4U;
        if(!decal_geoset_ptr(offset) ||
           !read(offset+0x44U,&id,4) ||
           !read(offset+0x4cU,&mesh_index,4))
            return {};
        node n{};
        n.index=id;n.mesh_index=mesh_index;
        if(id<num_nodes && mesh_index!=0xffffffffU) {
            uintptr_t const m=uintptr_t(pose_array)+uintptr_t(id)*48U;
            n.valid=decal_geoset_ptr(m) &&
                read(m,n.world.data(),48U) &&
                gpu_3do_matrix_finite(n.world);
        }
        if(n.valid && mesh_index<sets[uint32_t(current)].count) {
            uintptr_t const ptr=uintptr_t(sets[uint32_t(current)].base)+
                                uintptr_t(mesh_index)*0x70U;
            if(ptr==source_mesh && decal_geoset_same_pose(n.world,source_world))
                source_confirmed=true;
        }
        nodes.push_back(n);
    }
    if(!source_confirmed)return {};
    // Each alternate LOD is projected using its own hierarchy nodes and
    // composed joint matrices. A lower LOD can merge meshes, while a higher
    // LOD can split them: scan all nodes instead of mapping only the hit
    // mesh's index. Normal smoothing is performed independently per GeoSet.
    for(uint32_t g=0;g<num_geos;++g) {
        if(g==uint32_t(current))continue;
        std::unordered_map<uintptr_t,uint32_t> occurrences;
        for(auto const &n:nodes) {
            if(!n.valid || n.mesh_index>=sets[g].count)continue;
            uintptr_t const ptr=uintptr_t(sets[g].base)+
                                uintptr_t(n.mesh_index)*0x70U;
            if(!decal_geoset_ptr(ptr))return {};
            uint32_t const occurrence=++occurrences[ptr];
            result.push_back({ptr,g,n.index,occurrence,n.world});
            if(result.size()>768U)return {};
        }
    }
    return result;
}
}
