#pragma once
// Broadphase for the cached static world surfaces used by type-3 SPR decals.
// Build once per world snapshot. A query returns receiver indices in original
// surface order, so overlapping decals retain their previous draw ordering.
#include "sprite_decal_clip.hpp"
#include <algorithm>
#include <cstdint>
#include <limits>
#include <numeric>
#include <vector>

namespace jkgm {
inline bool decal_aabbs_overlap(
    decal_vec3 const &a_min,decal_vec3 const &a_max,
    decal_vec3 const &b_min,decal_vec3 const &b_max)
{
    for(int axis=0;axis<3;++axis)
        if(a_max[axis]<b_min[axis] || a_min[axis]>b_max[axis])
            return false;
    return true;
}

struct decal_bvh_node {
    decal_vec3 low{},high{};
    uint32_t begin=0,count=0;
    uint32_t left=0,right=0;
};

template<class Surface>
class decal_receiver_bvh {
public:
    void clear() { nodes_.clear();order_.clear(); }

    void build(std::vector<Surface> const &surfaces) {
        clear();
        if(surfaces.empty())return;
        order_.resize(surfaces.size());
        std::iota(order_.begin(),order_.end(),0U);
        nodes_.reserve(surfaces.size()/4U+2U);
        build_node(surfaces,0U,static_cast<uint32_t>(surfaces.size()));
    }

    size_t node_count() const { return nodes_.size(); }

    void query(std::vector<Surface> const &surfaces,
               decal_vec3 const &low,decal_vec3 const &high,
               std::vector<uint32_t> &out) const
    {
        out.clear();
        // Also support manually prepared receiver lists in test/tools.
        if(nodes_.empty() || order_.size()!=surfaces.size()) {
            for(size_t i=0;i<surfaces.size();++i) {
                auto const &s=surfaces[i];
                if(decal_aabbs_overlap(low,high,s.bounds_min,s.bounds_max))
                    out.push_back(static_cast<uint32_t>(i));
            }
            return;
        }
        std::vector<uint32_t> stack;
        stack.reserve(32);
        stack.push_back(0);
        while(!stack.empty()) {
            uint32_t const id=stack.back();
            stack.pop_back();
            auto const &node=nodes_[id];
            if(!decal_aabbs_overlap(low,high,node.low,node.high))
                continue;
            if(node.count!=0) {
                for(uint32_t i=node.begin;i<node.begin+node.count;++i) {
                    uint32_t const receiver=order_[i];
                    auto const &s=surfaces[receiver];
                    if(decal_aabbs_overlap(low,high,
                                           s.bounds_min,s.bounds_max))
                        out.push_back(receiver);
                }
            } else {
                stack.push_back(node.right);
                stack.push_back(node.left);
            }
        }
        // Traversal order depends on the BVH topology, not on the JK
        // surface array. Restore that order before clipping/triangulation.
        std::sort(out.begin(),out.end());
    }

private:
    uint32_t build_node(std::vector<Surface> const &surfaces,
                        uint32_t begin,uint32_t count)
    {
        uint32_t const id=static_cast<uint32_t>(nodes_.size());
        nodes_.emplace_back();
        decal_bvh_node node{};
        for(int a=0;a<3;++a) {
            node.low[a]=std::numeric_limits<float>::max();
            node.high[a]=-std::numeric_limits<float>::max();
        }
        decal_vec3 centroid_low=node.low,centroid_high=node.high;
        for(uint32_t i=begin;i<begin+count;++i) {
            auto const &s=surfaces[order_[i]];
            for(int a=0;a<3;++a) {
                node.low[a]=std::min(node.low[a],s.bounds_min[a]);
                node.high[a]=std::max(node.high[a],s.bounds_max[a]);
                float const center=s.bounds_min[a]*0.5f+s.bounds_max[a]*0.5f;
                centroid_low[a]=std::min(centroid_low[a],center);
                centroid_high[a]=std::max(centroid_high[a],center);
            }
        }
        constexpr uint32_t leaf_size=8;
        if(count<=leaf_size) {
            node.begin=begin;
            node.count=count;
        } else {
            int axis=0;
            for(int a=1;a<3;++a)
                if(centroid_high[a]-centroid_low[a]>
                   centroid_high[axis]-centroid_low[axis])axis=a;
            uint32_t const middle=begin+count/2U;
            std::nth_element(order_.begin()+begin,order_.begin()+middle,
                             order_.begin()+begin+count,
                [&](uint32_t a,uint32_t b) {
                    auto const &x=surfaces[a],&y=surfaces[b];
                    float const ca=x.bounds_min[axis]*0.5f+
                                   x.bounds_max[axis]*0.5f;
                    float const cb=y.bounds_min[axis]*0.5f+
                                   y.bounds_max[axis]*0.5f;
                    return ca==cb?a<b:ca<cb;
                });
            node.left=build_node(surfaces,begin,middle-begin);
            node.right=build_node(surfaces,middle,begin+count-middle);
        }
        nodes_[id]=node;
        return id;
    }
    std::vector<decal_bvh_node> nodes_;
    std::vector<uint32_t> order_;
};
}
