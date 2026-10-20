#pragma once
// Testable reference for Mysteries of the Sith collide=2: one collision
// SPHERE per mesh node, following its currently animated WORLD matrix.
// This is NOT polygon collision (which is the distinct collide=3 mode).
// Source: OpenJKDF2 sithIntersect_TreeIntersection.
// The engine uses 0.75 * rdMesh.radius per node, skips amputated subtrees,
// and selects the closest hit. This is intentionally NOT wired into the
// original JK23 collision loop without verified hook addresses.
#include "thing_node_math.hpp"
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <limits>
#include <vector>

namespace jkgm {
    constexpr float mots_mesh_collision_radius_scale=0.75f;
    struct mots_tree_node {
        int32_t parent=-1;
        bool has_mesh=false;
        bool amputated=false;
        float mesh_radius=0;
        gpu_3do_matrix34 world_matrix{};
    };
    struct mots_tree_hit {
        bool hit=false;
        uint32_t node=0;
        float distance=0;
        thing_node_vector center{};
        thing_node_vector normal{};
    };
    inline float mots_dot(thing_node_vector const &a,
                          thing_node_vector const &b)
    {
        return a[0]*b[0]+a[1]*b[1]+a[2]*b[2];
    }
    inline bool mots_tree_node_active(std::vector<mots_tree_node> const &nodes,
                                       uint32_t index)
    {
        // A hierarchy cycle or invalid parent is not a valid receiver.
        uint32_t hops=0;
        int32_t current=int32_t(index);
        while(current>=0 && hops++<nodes.size()) {
            if(size_t(current)>=nodes.size())return false;
            auto const &n=nodes[size_t(current)];
            if(n.amputated || n.parent==current)return false;
            current=n.parent;
        }
        return current<0;
    }
    inline bool mots_tree_sphere_cast(
        std::vector<mots_tree_node> const &nodes,
        thing_node_vector const &origin,
        thing_node_vector const &unit_direction,
        float max_distance,float moving_radius,mots_tree_hit &result)
    {
        if(nodes.empty() || nodes.size()>4096U ||
           !std::isfinite(max_distance) || !std::isfinite(moving_radius) ||
           max_distance<0 || moving_radius<0)
            return false;
        for(float x:origin)if(!std::isfinite(x))return false;
        for(float x:unit_direction)if(!std::isfinite(x))return false;
        if(std::fabs(mots_dot(unit_direction,unit_direction)-1)>0.001f)
            return false;
        mots_tree_hit nearest;
        float limit=max_distance;
        for(size_t i=0;i<nodes.size();++i) {
            auto const &n=nodes[i];
            if(!n.has_mesh || !mots_tree_node_active(nodes,uint32_t(i)))
                continue;
            if(!std::isfinite(n.mesh_radius) || n.mesh_radius<=0 ||
               !gpu_3do_matrix_finite(n.world_matrix))
                continue;
            thing_node_vector const center{{
                n.world_matrix[9],n.world_matrix[10],n.world_matrix[11]}};
            float const combined=n.mesh_radius*
                                 mots_mesh_collision_radius_scale+
                                 moving_radius;
            if(!std::isfinite(combined) || combined<=0)continue;
            thing_node_vector diff{{
                origin[0]-center[0],origin[1]-center[1],
                origin[2]-center[2]}};
            float const projected=mots_dot(diff,unit_direction);
            float const constant=mots_dot(diff,diff)-combined*combined;
            float const discriminant=projected*projected-constant;
            if(discriminant<0)continue;
            float const distance=constant<=0
                ?0.0f : -projected-std::sqrt(discriminant);
            if(distance<0 || distance>limit)continue;
            thing_node_vector norm{{
                diff[0]+distance*unit_direction[0],
                diff[1]+distance*unit_direction[1],
                diff[2]+distance*unit_direction[2]}};
            float const squared=mots_dot(norm,norm);
            if(squared>1.e-10f) {
                float const inv=1.0f/std::sqrt(squared);
                for(float &v:norm)v*=inv;
            } else {
                norm={{-unit_direction[0],-unit_direction[1],
                       -unit_direction[2]}};
            }
            nearest={true,uint32_t(i),distance,center,norm};
            limit=distance;
        }
        if(!nearest.hit)return false;
        result=nearest;
        return true;
    }
}
