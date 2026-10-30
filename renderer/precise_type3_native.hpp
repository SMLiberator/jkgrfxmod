#pragma once
// JKGM collide=3 zero-radius query using actual animated mesh nodes.
// Geometry is shared between Things; poses and amputations stay per-Thing.
// The original Sith collision caller still owns sector filtering and hits.
#include "precise_type3_raycast.hpp"
#include "mots_collision_snapshot_cache.hpp"
#include <algorithm>
#include <array>
#include <cstdint>
#include <cstring>
#include <functional>
#include <limits>
#include <vector>

namespace jkgm {
enum class precise_type3_status {unavailable,miss,hit};
struct precise_type3_hit {
    float distance=0.0f;
    precise_type3::vec3 normal{};
    uintptr_t mesh=0,face=0;
};
struct precise_type3_face {
    uintptr_t face_address=0,mesh_address=0;
    precise_type3::vec3 normal{};
    precise_type3::vec3 low{},high{};
    std::vector<uint32_t> indices;
};
struct precise_type3_bounds {
    precise_type3::vec3 low{},high{};
    bool valid=false;
};
struct precise_type3_stats {
    bool rebuilt=false;
    uint32_t nodes=0,nodes_in_bounds=0;
    uint32_t faces_in_bounds=0,polygon_tests=0;
};
struct precise_type3_cache_entry {
    mots_collision_model_identity key{};
    uint64_t world=0,generation=0,built_ms=0,last_used_ms=0;
    mots_native_model_snapshot geometry{};
    std::vector<std::vector<precise_type3_face>> faces;
    std::vector<precise_type3_bounds> node_bounds;
};
class precise_type3_native_cache {
    static constexpr size_t max_entries=16;
    static constexpr uint32_t max_faces=32768;
    static constexpr uint32_t max_indices=524288;
    static constexpr uint32_t max_face_vertices=512;
    std::vector<precise_type3_cache_entry> entries_;
    uint64_t world_=0;

    static bool build(thing_node_reader const &read,uintptr_t thing,
                      precise_type3_cache_entry &entry) {
        using namespace mots_native_layout;
        mots_native_model_snapshot geometry{};
        if(!read_native_collision_model(read,thing,geometry))return false;
        precise_type3_cache_entry candidate{};
        candidate.geometry=std::move(geometry);
        candidate.faces.resize(candidate.geometry.nodes.size());
        candidate.node_bounds.resize(candidate.geometry.nodes.size());
        uint32_t face_count=0,index_count=0;
        for(size_t node_index=0;
            node_index<candidate.geometry.nodes.size();++node_index) {
            auto const &node=candidate.geometry.nodes[node_index];
            if(node.mesh_index<0 || node.local_vertices.size()<3)continue;
            // Shared immutable vertex array bounds, in mesh-local space.
            // They include authored offset vertices and do not depend on
            // the Thing's original, often inaccurate collision sphere.
            auto &bounds=candidate.node_bounds[node_index];
            bounds.low={{std::numeric_limits<float>::max(),
                         std::numeric_limits<float>::max(),
                         std::numeric_limits<float>::max()}};
            bounds.high={{-std::numeric_limits<float>::max(),
                          -std::numeric_limits<float>::max(),
                          -std::numeric_limits<float>::max()}};
            for(auto const &vertex:node.local_vertices)
                for(int axis=0;axis<3;++axis) {
                    bounds.low[axis]=std::min(bounds.low[axis],vertex[axis]);
                    bounds.high[axis]=std::max(bounds.high[axis],vertex[axis]);
                }
            bounds.valid=true;
            uintptr_t const mesh=entry.key.meshes+
                 uintptr_t(node.mesh_index)*mesh_stride;
            uint32_t faces_ptr=0,n_faces=0;
            if(!mots_native_detail::read_u32(read,mesh+0x40,faces_ptr) ||
               !mots_native_detail::read_u32(read,mesh+0x50,n_faces) ||
               n_faces>8192U || n_faces>max_faces-face_count ||
               !thing_node_valid_address(faces_ptr,0x40))
                return false;
            face_count+=n_faces;
            auto &output=candidate.faces[node_index];
            output.reserve(n_faces);
            for(uint32_t face_index=0;face_index<n_faces;++face_index) {
                uintptr_t const addr=uintptr_t(faces_ptr)+
                                     uintptr_t(face_index)*0x40U;
                uint32_t count=0,indices_addr=0;
                precise_type3::vec3 normal{};
                if(!mots_native_detail::read_u32(read,addr+0x14,count) ||
                   count>max_face_vertices ||
                   !read(addr+0x34,normal.data(),12))
                    return false;
                if(count<3)continue;
                if(count>max_indices-index_count ||
                   !mots_native_detail::read_u32(read,addr+0x18,indices_addr) ||
                   !thing_node_valid_address(indices_addr,size_t(count)*4U))
                    return false;
                index_count+=count;
                precise_type3_face face{};
                face.mesh_address=mesh;
                face.face_address=addr;
                face.normal=normal;
                face.indices.resize(count);
                if(!read(indices_addr,face.indices.data(),count*4U))
                    return false;
                for(uint32_t index:face.indices)
                    if(index>=node.local_vertices.size())return false;
                if(!precise_type3::finite(face.normal))return false;
                face.low={{std::numeric_limits<float>::max(),
                           std::numeric_limits<float>::max(),
                           std::numeric_limits<float>::max()}};
                face.high={{-std::numeric_limits<float>::max(),
                            -std::numeric_limits<float>::max(),
                            -std::numeric_limits<float>::max()}};
                for(uint32_t const index:face.indices) {
                    auto const &v=node.local_vertices[index];
                    for(int axis=0;axis<3;++axis) {
                        face.low[axis]=std::min(face.low[axis],v[axis]);
                        face.high[axis]=std::max(face.high[axis],v[axis]);
                    }
                }
                output.push_back(std::move(face));
            }
        }
        entry.geometry=std::move(candidate.geometry);
        entry.faces=std::move(candidate.faces);
        entry.node_bounds=std::move(candidate.node_bounds);
        return true;
    }
public:
    void clear(){entries_.clear();world_=0;}
    precise_type3_status cast(
        thing_node_reader const &read,uintptr_t checked,
        uint64_t world,uint64_t generation,bool reliable_revision,
        uint64_t timestamp_ms,mots_native_pose_reader const &pose,
        precise_type3::vec3 const &start,
        precise_type3::vec3 const &unit_direction,
        float max_distance,precise_type3_hit &result,
        precise_type3_stats *stats=nullptr)
    {
        if(world_!=world) {entries_.clear();world_=world;}
        if(!world || !read || !pose || !checked ||
           !(max_distance>0.0f) || !std::isfinite(max_distance) ||
           !precise_type3::finite(start) ||
           !precise_type3::finite(unit_direction))return precise_type3_status::unavailable;
        double len2=0;
        for(float x:unit_direction)len2+=double(x)*x;
        if(std::fabs(len2-1.0)>0.001)return precise_type3_status::unavailable;
        mots_collision_model_identity identity{};
        if(!read_mots_collision_identity(read,checked,identity))
            return precise_type3_status::unavailable;
        precise_type3_cache_entry *chosen=nullptr;
        for(auto &entry:entries_) {
            if(entry.world==world && entry.key.same_geometry(identity)) {
                chosen=&entry;
                break;
            }
        }
        precise_type3_stats metrics{};
        bool const refresh=chosen==nullptr ||
            chosen->generation!=generation ||
            (timestamp_ms<chosen->built_ms) ||
            (!reliable_revision &&
             timestamp_ms-chosen->built_ms>1000);
        if(refresh) {
            metrics.rebuilt=true;
            precise_type3_cache_entry rebuilt{};
            rebuilt.key=identity;
            rebuilt.world=world;
            rebuilt.generation=generation;
            rebuilt.built_ms=timestamp_ms;
            rebuilt.last_used_ms=timestamp_ms;
            if(!build(read,checked,rebuilt))
                return precise_type3_status::unavailable;
            if(!chosen) {
                if(entries_.size()>=max_entries) {
                    auto const oldest=std::min_element(
                        entries_.begin(),entries_.end(),
                        [](auto const &a,auto const &b){
                            return a.last_used_ms<b.last_used_ms;
                        });
                    entries_.erase(oldest);
                }
                entries_.push_back(std::move(rebuilt));
                chosen=&entries_.back();
            } else *chosen=std::move(rebuilt);
        }
        chosen->last_used_ms=timestamp_ms;
        // Live amputation flags, not cached across different Things.
        auto const size=chosen->geometry.nodes.size();
        std::vector<uint32_t> amputated(size,0);
        if(identity.amputations &&
           !read(identity.amputations,amputated.data(),size*4U))
            return precise_type3_status::unavailable;
        // Reuse per-Thing active node decisions once per query instead of
        // repeatedly traversing every parent chain for every leaf.
        // Native snapshot validation has already ruled out cycles.
        std::vector<int8_t> active(size,-1);
        for(size_t i=0;i<size;++i) {
            if(active[i]>=0)continue;
            std::vector<size_t> chain;
            int32_t at=int32_t(i);
            while(at>=0 && active[size_t(at)]<0) {
                if(size_t(at)>=size || chain.size()>=size)
                    return precise_type3_status::unavailable;
                chain.push_back(size_t(at));
                at=chosen->geometry.nodes[size_t(at)].parent;
            }
            bool enabled=at<0 || active[size_t(at)]!=0;
            for(auto it=chain.rbegin();it!=chain.rend();++it) {
                enabled=enabled && amputated[*it]==0;
                active[*it]=enabled?1:0;
            }
        }

        precise_type3_hit best{};
        best.distance=max_distance;
        bool found=false;
        for(size_t i=0;i<size;++i) {
            if(!active[i] || chosen->faces[i].empty())continue;
            ++metrics.nodes;
            gpu_3do_matrix34 world_pose{};
            if(!pose(uint32_t(i),world_pose))
                return precise_type3_status::unavailable;
            auto const t=precise_type3::invert(world_pose);
            if(!t.valid)return precise_type3_status::unavailable;
            auto const ray=precise_type3::to_local(
                t,start,unit_direction);
            auto const &bounds=chosen->node_bounds[i];
            if(bounds.valid && !precise_type3::ray_intersects_bounds(
                ray,bounds.low,bounds.high,
                found?double(best.distance):double(max_distance)))
                continue;
            ++metrics.nodes_in_bounds;
            auto const &vertices=chosen->geometry.nodes[i].local_vertices;
            for(auto const &face:chosen->faces[i]) {
                if(!precise_type3::ray_intersects_bounds(
                    ray,face.low,face.high,
                    found?double(best.distance):double(max_distance)))
                    continue;
                ++metrics.faces_in_bounds;
                double distance=0.0;
                auto const vertex=[&](size_t v) {
                    return vertices[face.indices[v]];
                };
                ++metrics.polygon_tests;
                if(!precise_type3::hit_face(
                    ray,face.normal,face.indices.size(),vertex,
                    found?double(best.distance):double(max_distance),
                    distance))
                    continue;
                precise_type3::vec3 world_normal{};
                if(!precise_type3::world_normal(
                       t,face.normal,world_normal))
                    return precise_type3_status::unavailable;
                best.distance=float(distance);
                best.normal=world_normal;
                best.mesh=face.mesh_address;
                best.face=face.face_address;
                found=true;
            }
        }
        if(stats)*stats=metrics;
        if(!found)return precise_type3_status::miss;
        result=best;
        return precise_type3_status::hit;
    }
};
} // namespace jkgm
