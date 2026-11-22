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
#include <functional>
#include <unordered_map>
#include <vector>

namespace jkgm {
    struct planar_reflection_native_node {
        uintptr_t thing=0,model=0,mesh=0;
        uint32_t geoset=0,hierarchy_index=0,occurrence=0;
        uint64_t owner=0;
        gpu_3do_matrix34 world_transform{};
    };


    // Immutable hierarchy relationships are shared by every Thing using the
    // same model/geoset. Native COG mutators invalidate this cache via the
    // global model revision (NOT the narrower mesh-source generation).
    struct planar_reflection_model_topology {
        struct node {
            int32_t parent=-1;
            uint32_t mesh=0xffffffffU;
            uint32_t whole_occurrence=0;
        };
        uintptr_t model=0;
        uint32_t geo=0,count=0,hierarchy=0,meshes_count=0,meshes=0;
        std::vector<node> nodes;
        // Parent-before-child order, regardless of native array order.
        // Cycles and bad parent pointers are rejected during construction.
        std::vector<uint32_t> parent_first;
    };

    template<class Read>
    bool load_planar_reflection_model_topology(
        Read const &read,uintptr_t model,uint32_t geo,uint32_t count,
        uint32_t hierarchy,uint32_t meshes_count,uint32_t meshes,
        planar_reflection_model_topology &result)
    {
        using namespace mots_native_layout;
        planar_reflection_model_topology fresh{};
        fresh.model=model;fresh.geo=geo;fresh.count=count;
        fresh.hierarchy=hierarchy;fresh.meshes_count=meshes_count;
        fresh.meshes=meshes;
        fresh.nodes.resize(count);
        for(uint32_t i=0;i<count;++i) {
            uintptr_t const at=uintptr_t(hierarchy)+uintptr_t(i)*node_stride;
            uint32_t id=0,mesh=0,parent=0;
            if(!thing_node_valid_address(at,0x54U) ||
               !read(at+node_index,&id,4) ||
               !read(at+node_mesh,&mesh,4) ||
               !read(at+node_parent,&parent,4) ||
               id!=i ||
               !mots_native_detail::hierarchy_pointer_to_index(
                   parent,hierarchy,count,fresh.nodes[i].parent) ||
               fresh.nodes[i].parent==int32_t(i))
                return false;
            fresh.nodes[i].mesh=mesh;
        }
        // Validate every parent chain, including empty/amputated nodes.
        // Build a topological order once instead of O(nodes * depth)
        // ancestor walks for every animated Thing.
        std::vector<uint8_t> state(count,0U);
        std::vector<uint32_t> chain;
        fresh.parent_first.reserve(count);
        for(uint32_t i=0;i<count;++i) {
            if(state[i]==2U)continue;
            chain.clear();
            int32_t at=int32_t(i);
            while(at>=0 && state[uint32_t(at)]==0U) {
                state[uint32_t(at)]=1U;
                chain.push_back(uint32_t(at));
                at=fresh.nodes[uint32_t(at)].parent;
            }
            if(at>=0 && state[uint32_t(at)]==1U)return false;
            for(auto it=chain.rbegin();it!=chain.rend();++it) {
                state[*it]=2U;
                fresh.parent_first.push_back(*it);
            }
        }
        // No-amputation fast path: repeated rdMesh occurrences have fixed
        // ordinals. With amputations we recalculate only the live ordinals.
        std::vector<uint32_t> occurrence(meshes_count,0U);
        for(auto &node:fresh.nodes)
            if(node.mesh<meshes_count)
                node.whole_occurrence=++occurrence[node.mesh];
        result=std::move(fresh);
        return true;
    }

    class planar_reflection_model_topology_cache {
        struct model_key {
            uintptr_t model=0;
            uint32_t geo=0;
            bool operator==(model_key const &other) const {
                return model==other.model && geo==other.geo;
            }
        };
        struct model_key_hash {
            size_t operator()(model_key const &key) const {
                return std::hash<uintptr_t>{}(key.model) ^
                    (std::hash<uint32_t>{}(key.geo)+size_t(0x9e3779b9U));
            }
        };
        std::unordered_map<model_key,planar_reflection_model_topology,
            model_key_hash> entries_;
        uintptr_t world_=0;
        uint64_t revision_=0,frame_=0;
        bool reliable_=false;
    public:
        void clear() {
            entries_.clear();
            world_=0;revision_=0;frame_=0;reliable_=false;
        }
        void prepare(uintptr_t world,uint64_t revision,
                     bool reliable,uint64_t frame) {
            // Without the complete COG mutation hooks, at most reuse
            // topology within a single rendering frame.
            if(world_!=world || revision_!=revision ||
               reliable_!=reliable || (!reliable && frame_!=frame))
                entries_.clear();
            world_=world;revision_=revision;frame_=frame;
            reliable_=reliable;
        }
        template<class Read>
        bool get(Read const &read,uintptr_t model,uint32_t geo,
                 uint32_t count,uint32_t hierarchy,
                 uint32_t meshes_count,uint32_t meshes,
                 planar_reflection_model_topology const *&result) {
            result=nullptr;
            model_key const key{model,geo};
            auto found=entries_.find(key);
            if(found!=entries_.end()) {
                auto const &entry=found->second;
                if(entry.count==count && entry.hierarchy==hierarchy &&
                   entry.meshes_count==meshes_count && entry.meshes==meshes) {
                    result=&entry;
                    return true;
                }
                entries_.erase(found);
            }
            planar_reflection_model_topology fresh{};
            if(!load_planar_reflection_model_topology(
                read,model,geo,count,hierarchy,meshes_count,meshes,fresh))
                return false;
            // Bound memory without flushing all hot models when a large
            // cloned-model city exceeds the cache capacity.
            if(entries_.size()>=4096U)entries_.erase(entries_.begin());
            result=&entries_.emplace(key,std::move(fresh)).first->second;
            return true;
        }
    };

    // Source pointer and signature are rechecked after discovery: the game
    // can recycle a Thing or switch its model/GeoSet before capture.
    // Failed reads and invalid parent trees never emit partial geometry.
    template<class Read,class Refresh>
    bool collect_planar_reflection_native_nodes(
        Read const &read,planar_reflection_model_thing const &source,
        Refresh const &refresh,std::vector<planar_reflection_native_node> &out,
        bool source_geoset_zero=false,
        planar_reflection_model_topology_cache *topology_cache=nullptr)
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
        // Normal JK LOD is mutable; auto GPU LOD starts at GeoSet 0.
        uint32_t const geo=source_geoset_zero ? 0U :
            read_sprite_decal_active_geoset(
                read,current.rd_thing,source.model);
        if(geo==0xffffffffU ||
           !read(source.model+model_geosets+
                 uintptr_t(geo)*geoset_stride,&meshes_count,4) ||
           !read(source.model+model_geosets+
                 uintptr_t(geo)*geoset_stride+4,&meshes,4) ||
           meshes_count>max_meshes ||
           (meshes_count && !thing_node_valid_address(meshes,mesh_stride)))
            return false;

        planar_reflection_model_topology fresh{};
        planar_reflection_model_topology const *topology=nullptr;
        if(topology_cache) {
            if(!topology_cache->get(read,source.model,geo,count,hierarchy,
                                    meshes_count,meshes,topology))
                return false;
        } else {
            if(!load_planar_reflection_model_topology(
                read,source.model,geo,count,hierarchy,
                meshes_count,meshes,fresh))
                return false;
            topology=&fresh;
        }

        // Amputation flags are live per-Thing state. Evaluate visibility in
        // cached parent-first order in O(nodes), including hidden branches.
        std::vector<uint8_t> active;
        if(amputations) {
            active.assign(count,1U);
            std::vector<uint32_t> cut(count,0U);
            for(uint32_t i=0;i<count;++i) {
                uintptr_t const at=uintptr_t(amputations)+uintptr_t(i)*4U;
                if(!thing_node_valid_address(at,4U) ||
                   !read(at,&cut[i],4U))return false;
            }
            for(uint32_t const i:topology->parent_first) {
                int32_t const parent=topology->nodes[i].parent;
                active[i]=(!cut[i] &&
                           (parent<0 || active[uint32_t(parent)]))?1U:0U;
            }
        }

        // Refresh once, then read the CURRENT complete joint pose array.
        std::vector<gpu_3do_matrix34> poses;
        if(!precise_type3_read_pose_batch(
               read,current.rd_thing,count,
               [&](gpu_3do_matrix34 &first)->bool {
                   return refresh(current.rd_thing,current.world_placement,
                                  first);
               },poses))
            return false;
        std::vector<uint32_t> occurrences;
        if(amputations)occurrences.assign(meshes_count,0U);
        std::vector<planar_reflection_native_node> found;
        found.reserve(count);
        for(uint32_t i=0;i<count;++i) {
            auto const &node=topology->nodes[i];
            if((amputations && !active[i]) ||
               node.mesh==0xffffffffU || node.mesh>=meshes_count)
                continue;
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
            entry.occurrence=amputations
                ?++occurrences[node.mesh]:node.whole_occurrence;
            entry.owner=(uint64_t(uint32_t(source.thing))<<32)|
                         source.signature;
            entry.world_transform=poses[i];
            found.push_back(entry);
        }
        out=std::move(found);
        return true;
    }

} // namespace jkgm
