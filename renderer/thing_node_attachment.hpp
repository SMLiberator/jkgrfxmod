#pragma once
// JKGM node attachments: small sidecar for native SITH_ATTACH_THING.
// The engine owns parent/child links and DetachThing lifecycle. This registry
// only remembers the animated node, offset and optional rotation inheritance.
#include "thing_node_math.hpp"
#include <cmath>
#include <cstdint>
#include <map>
#include <mutex>
#include <vector>
namespace jkgm {
    constexpr uint32_t node_attach_copy_rotation=0x1U;
    constexpr uint32_t node_attach_allowed_flags=node_attach_copy_rotation;
    constexpr uint32_t native_attach_thing=0x4U;
    constexpr uint32_t native_attach_no_move=0x8U;
    struct node_attachment {
        uintptr_t child=0,parent=0;
        uint32_t child_signature=0,parent_signature=0;
        int node=-1;
        uint32_t flags=node_attach_copy_rotation;
        thing_node_vector offset{{0,0,0}};
    };
    inline bool node_attachment_valid(node_attachment const &a) {
        if(a.child<0x10000 || a.parent<0x10000 || a.child==a.parent ||
           a.node<0 || a.node>=4096 ||
           (a.flags & ~node_attach_allowed_flags)!=0)return false;
        for(float x:a.offset)if(!std::isfinite(x) || std::fabs(x)>256.0f)
            return false;
        return true;
    }
    inline bool compute_node_attachment_pose(
        node_attachment const &a,thing_node_pose const &node,
        thing_node_pose const &child_current,thing_node_pose &result)
    {
        if(!node_attachment_valid(a))return false;
        // Node matrices may carry artist-authored nonuniform scale/mirroring.
        // The native SithThing orientation must be unit-length for physics.
        auto normalize=[](thing_node_vector &axis) {
            float n=0.0f;
            for(float f:axis)n+=f*f;
            if(!std::isfinite(n) || n<1e-10f)return false;
            n=1.0f/std::sqrt(n);
            for(float &f:axis)f*=n;
            return true;
        };
        result=(a.flags&node_attach_copy_rotation)?node:child_current;
        if(!normalize(result.right) || !normalize(result.look) ||
           !normalize(result.up))return false;
        // Offset retains actual node scale, mirroring and orientation.
        result.origin=node.origin;
        for(int i=0;i<3;++i) {
            result.origin[i]+=node.right[i]*a.offset[0]+
                              node.look[i]*a.offset[1]+
                              node.up[i]*a.offset[2];
            if(!std::isfinite(result.origin[i]))return false;
        }
        return true;
    }
    class node_attachment_registry {
        mutable std::mutex mutex_;
        std::map<uintptr_t,node_attachment> entries_;
        uint32_t world_=0;
    public:
        void prepare_world(uint32_t world) {
            if(!world)return;
            std::lock_guard<std::mutex> lock(mutex_);
            if(world_ && world_!=world)entries_.clear();
            world_=world;
        }
        bool set(node_attachment const &a) {
            if(!node_attachment_valid(a))return false;
            std::lock_guard<std::mutex> lock(mutex_);
            // Reject parent chains that would form attachment cycles.
            uintptr_t current=a.parent;
            for(size_t depth=0;depth<=entries_.size();++depth) {
                if(current==a.child)return false;
                auto it=entries_.find(current);
                if(it==entries_.end())break;
                current=it->second.parent;
                if(depth==entries_.size())return false;
            }
            if(entries_.size()>=256 && !entries_.count(a.child))return false;
            entries_[a.child]=a;
            return true;
        }
        void erase(uintptr_t child) {
            std::lock_guard<std::mutex> lock(mutex_);
            entries_.erase(child);
        }
        std::vector<node_attachment> snapshot()const {
            std::lock_guard<std::mutex> lock(mutex_);
            std::vector<node_attachment> out;
            out.reserve(entries_.size());
            for(auto const &e:entries_)out.push_back(e.second);
            return out;
        }
    };
    inline node_attachment_registry &get_node_attachment_registry() {
        static node_attachment_registry registry;
        return registry;
    }
}
