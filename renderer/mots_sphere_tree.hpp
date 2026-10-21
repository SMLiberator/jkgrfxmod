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
        // Optional collision-only recentering. When enabled, mesh_radius
        // is a conservative radius around local_center (not the 3DO's
        // original origin-centered Radius parameter).
        thing_node_vector local_center{};
        bool center_on_geometry=false;
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
    // Transform a mesh's local collision sphere with its animated node
    // matrix. Legacy mode reproduces MotS's radius * 0.75, centered at the
    // joint; opt-in mode centers on the actual mesh geometry and does NOT
    // shrink the fitted sphere (which could omit vertices).
    inline bool mots_tree_world_sphere(
        mots_tree_node const &node,
        thing_node_vector &center, float &radius)
    {
        if(!node.has_mesh || !std::isfinite(node.mesh_radius) ||
           node.mesh_radius<=0.0f || !gpu_3do_matrix_finite(node.world_matrix))
            return false;
        thing_node_vector world{};
        float scale=mots_mesh_collision_radius_scale;
        if(node.center_on_geometry) {
            if(!get_thing_node_point(node.world_matrix,node.local_center,world))
                return false;
            double len2[3]{};
            for(int axis=0;axis<3;++axis) {
                for(int comp=0;comp<3;++comp) {
                    double const v=node.world_matrix[axis*3+comp];
                    len2[axis]+=v*v;
                }
            }
            bool orthogonal=true;
            for(int a=0;a<3;++a) {
                for(int b=a+1;b<3;++b) {
                    double dot=0;
                    for(int k=0;k<3;++k)
                        dot+=double(node.world_matrix[a*3+k])*
                             double(node.world_matrix[b*3+k]);
                    if(std::fabs(dot)>1.e-5*std::sqrt(len2[a]*len2[b]))
                        orthogonal=false;
                }
            }
            // Max axis length for orthogonal R/L/U; Frobenius norm for
            // unusual sheared matrices ensures a conservative bound.
            double const scale_double=std::sqrt(
                orthogonal
                    ? std::max(len2[0],std::max(len2[1],len2[2]))
                    : len2[0]+len2[1]+len2[2]);
            if(!std::isfinite(scale_double) || scale_double<=0 ||
               scale_double>double(std::numeric_limits<float>::max()))
                return false;
            scale=static_cast<float>(scale_double);
        } else {
            world={{node.world_matrix[9],
                    node.world_matrix[10],node.world_matrix[11]}};
        }
        double const r=double(node.mesh_radius)*double(scale);
        if(!std::isfinite(r) || r>double(std::numeric_limits<float>::max()) ||
           r<=0.0) return false;
        // Never underbound an opt-in fitted sphere due to rounding.
        float fitted=static_cast<float>(r);
        if(node.center_on_geometry && double(fitted)<r)
            fitted=std::nextafter(fitted,
                std::numeric_limits<float>::infinity());
        center=world;
        radius=fitted;
        return true;
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
            if(n.amputated || n.parent==current || n.parent < -1)return false;
            current=n.parent;
        }
        return current<0;
    }
    // Conservative radius around the Thing's world-space origin that
    // encloses all active node spheres in the current animated pose.
    // This can be used as a dynamically derived broad-phase bound later:
    // no separately authored treeSize is needed.
    inline bool mots_tree_enclosing_radius(
        std::vector<mots_tree_node> const &nodes,
        thing_node_vector const &thing_origin,
        float &result)
    {
        if(nodes.empty() || nodes.size()>4096U)return false;
        for(float x:thing_origin)if(!std::isfinite(x))return false;
        bool found=false;
        double furthest=0.0;
        for(size_t i=0;i<nodes.size();++i) {
            if(!mots_tree_node_active(nodes,uint32_t(i)) ||
               !nodes[i].has_mesh)continue;
            thing_node_vector center{};
            float radius=0;
            // A partially constructed broad phase would be unsafe:
            // if an active mesh cannot be bounded, let the caller bypass
            // the broad-phase rather than reject possible hits.
            if(!mots_tree_world_sphere(nodes[i],center,radius))return false;
            double distance_squared=0.0;
            for(int k=0;k<3;++k) {
                double const d=double(center[k])-thing_origin[k];
                distance_squared+=d*d;
            }
            double const reach=std::sqrt(distance_squared)+radius;
            if(!std::isfinite(reach) ||
               reach>double(std::numeric_limits<float>::max()))
                return false;
            furthest=std::max(furthest,reach);
            found=true;
        }
        if(!found)return false;
        float bound=static_cast<float>(furthest);
        if(double(bound)<furthest)
            bound=std::nextafter(bound,std::numeric_limits<float>::infinity());
        result=bound;
        return true;
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
            thing_node_vector center{};
            float mesh_radius=0.0f;
            if(!mots_tree_world_sphere(n,center,mesh_radius))continue;
            float const combined=mesh_radius+moving_radius;
            if(!std::isfinite(combined) || combined<=0)continue;
            thing_node_vector diff{{
                origin[0]-center[0],origin[1]-center[1],
                origin[2]-center[2]}};
            float const projected=mots_dot(diff,unit_direction);
            float const constant=mots_dot(diff,diff)-combined*combined;
            float const discriminant=projected*projected-constant;
            // The native zero-length test only reports strict overlap;
            // starting precisely on the sphere boundary is not enough.
            if(max_distance==0.0f && constant>=0.0f)continue;
            // The JK/MotS sphere test rejects exact tangency (distance
            // from center to ray >= radius), including zero-length casts.
            if(discriminant<=0)continue;
            float const distance=constant<0
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
