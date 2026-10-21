#pragma once
// Bounded JKGM type-2 collision *geometry* cache.
//
// Fitted mesh spheres and hierarchy are model-level data, not per-probe
// or per-Thing data. After fitting, the full vertex arrays are discarded.
// All Things sharing a selected model/geoset use one geometry snapshot.
// Animation matrices and amputation flags are still read for each query.
// A true in-place vertex edit needs explicit cache invalidation/model reload.
// Does not cache any Thing pointer dereference beyond validated metadata.
#include "mots_native_mesh_snapshot.hpp"
#include <array>
#include <cstdint>
#include <memory>
#include <vector>

namespace jkgm {
    struct mots_collision_model_identity {
        uintptr_t thing=0,model=0,hierarchy=0,meshes=0,amputations=0;
        uint32_t thing_signature=0,kind=0,rd_type=0;
        uint32_t geoset=0,geos=0,node_count=0,mesh_count=0;
        bool operator==(mots_collision_model_identity const &o) const {
            return thing==o.thing && model==o.model &&
                hierarchy==o.hierarchy && meshes==o.meshes &&
                amputations==o.amputations &&
                thing_signature==o.thing_signature && kind==o.kind &&
                rd_type==o.rd_type && geoset==o.geoset &&
                geos==o.geos && node_count==o.node_count &&
                mesh_count==o.mesh_count;
        }
        bool same_geometry(mots_collision_model_identity const &o) const {
            // Thing identity and limb amputation are NOT model geometry.
            return model==o.model && hierarchy==o.hierarchy &&
                meshes==o.meshes && geoset==o.geoset &&
                geos==o.geos && node_count==o.node_count &&
                mesh_count==o.mesh_count;
        }
    };

    inline bool read_mots_collision_identity(
        thing_node_reader const &read,uintptr_t thing,
        mots_collision_model_identity &output)
    {
        using namespace mots_native_layout;
        using mots_native_detail::read_u32;
        if(!read || !thing_node_valid_address(thing,0xe8))return false;
        mots_collision_model_identity k{};
        k.thing=thing;
        uint32_t model=0,selected=0,preferred=0,amputation_ptr=0;
        if(!read_u32(read,thing+0x0c,k.kind) ||
           k.kind==0 || k.kind>15 ||
           !read_u32(read,thing+0x74,k.thing_signature) ||
           !read_u32(read,thing+0xc4,k.rd_type) ||
           k.rd_type!=1 ||
           !read_u32(read,thing+0xc8,model) ||
           !thing_node_valid_address(model,0x60) ||
           !read_u32(read,thing+0xc4+rd_geoset_select,selected) ||
           !read_u32(read,thing+0xc4+rd_amputations,amputation_ptr))
            return false;
        k.amputations=amputation_ptr;
        k.model=model;
        uint32_t hierarchy=0,meshes=0;
        if(!read_u32(read,model+model_num_geos,k.geos) ||
           !read_u32(read,model+model_default_geoset,preferred) ||
           !read_u32(read,model+model_num_nodes,k.node_count) ||
           !read_u32(read,model+model_hierarchy,hierarchy) ||
           k.geos==0 || k.geos>4 ||
           k.node_count==0 || k.node_count>max_nodes ||
           !thing_node_valid_address(hierarchy,node_stride))
            return false;
        if(selected==0xffffffffU)selected=preferred;
        if(selected>=k.geos)return false;
        k.geoset=selected;
        k.hierarchy=hierarchy;
        uintptr_t const geo=uintptr_t(model)+model_geosets+
                            uintptr_t(selected)*geoset_stride;
        if(!read_u32(read,geo,k.mesh_count) ||
           !read_u32(read,geo+4,meshes) ||
           k.mesh_count==0 || k.mesh_count>max_meshes ||
           !thing_node_valid_address(meshes,mesh_stride))
            return false;
        k.meshes=meshes;
        output=k;
        return true;
    }

    struct mots_collision_snapshot_counters {
        uint64_t hits=0,rebuilds=0,failed=0,evicted=0;
    };

    class mots_collision_snapshot_cache {
    public:
        static constexpr size_t max_entries=24;
        // The borrowed result remains valid only until the next get/clear.
        bool get(thing_node_reader const &read,uintptr_t thing,
                 uint64_t now_ms,mots_native_model_snapshot const *&out,
                 bool *was_rebuilt=nullptr)
        {
            mots_collision_model_identity identity{};
            if(!read_mots_collision_identity(read,thing,identity)) {
                ++counts_.failed;
                return false;
            }
            entry *slot=nullptr;
            for(auto &candidate:entries_)
                if(candidate.key.same_geometry(identity)) {
                    slot=&candidate;
                    break;
                }
            bool const reuse=slot!=nullptr;
            if(!reuse) {
                mots_native_model_snapshot snapshot{};
                if(!read_native_collision_model(read,thing,snapshot)) {
                    ++counts_.failed;
                    return false;
                }
                if(!slot) {
                    if(entries_.size()>=max_entries) {
                        size_t oldest=0;
                        for(size_t i=1;i<entries_.size();++i)
                            if(entries_[i].last_used_ms<
                               entries_[oldest].last_used_ms)
                                oldest=i;
                        entries_.erase(entries_.begin()+oldest);
                        ++counts_.evicted;
                    }
                    entries_.emplace_back();
                    slot=&entries_.back();
                }
                // The native renderer still owns its vertices/pivots.
                // This retained collision-only snapshot needs only each
                // mesh's fitted local center, radius and hierarchy links.
                for(auto &node:snapshot.nodes)
                    std::vector<thing_node_vector>().swap(node.local_vertices);
                slot->key=identity;
                slot->snapshot=std::move(snapshot);
                ++counts_.rebuilds;
            } else ++counts_.hits;

            // A model's vertex data may be immutable, but limb amputation
            // is per-Thing and can change between consecutive raycasts.
            // Bulk-read all flags once; a failed read is NOT a collision miss.
            if(identity.amputations) {
                std::array<uint32_t,mots_native_layout::max_nodes> flags{};
                size_t const bytes=size_t(identity.node_count)*4;
                if(!thing_node_valid_address(identity.amputations,bytes) ||
                   !read(identity.amputations,flags.data(),bytes)) {
                    ++counts_.failed;
                    return false;
                }
                for(size_t i=0;i<identity.node_count;++i)
                    slot->snapshot.nodes[i].amputated=flags[i]!=0;
            } else {
                for(auto &node:slot->snapshot.nodes)
                    node.amputated=false;
            }
            slot->last_used_ms=now_ms;
            if(was_rebuilt)*was_rebuilt=!reuse;
            out=&slot->snapshot;
            return true;
        }
        mots_collision_snapshot_counters const &counters() const {
            return counts_;
        }
        size_t size() const {return entries_.size();}
        void clear(){entries_.clear();counts_={};}
    private:
        struct entry {
            mots_collision_model_identity key{};
            uint64_t last_used_ms=0;
            mots_native_model_snapshot snapshot{};
        };
        std::vector<entry> entries_;
        mots_collision_snapshot_counters counts_{};
    };
}
