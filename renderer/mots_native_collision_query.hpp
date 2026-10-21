#pragma once
// Staged collide=2 collision query for verified JKGM 3DO snapshots.
//
// Does not modify engine memory or install a detour. The future x86 hook
// must call the original collision routine unchanged for modes 1 and 3,
// and only invoke this opt-in query when type 2 is explicitly enabled.
#include "mots_collision_dispatch.hpp"
#include "mots_native_mesh_snapshot.hpp"
#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <limits>

namespace jkgm {

    enum class mots_query_status {
        unavailable, // invalid/native input: caller must use safe fallback
        miss,        // a complete valid type-2 query found no hit
        hit          // hit result is valid
    };
    struct mots_collision_query_hit {
        uint32_t node=0;
        float distance=0.0f;
        thing_node_vector normal{};
        thing_node_vector sphere_center{};
        bool reverse_cast=false;
    };
    struct mots_collision_query_stats {
        bool tested_broadphase=false;
        bool rejected_broadphase=false;
        uint32_t active_meshes=0;
    };

    // A conservative swept-sphere vs enclosing-sphere rejection.
    // Unlike the actual hit test, an exact tangent must NEVER be culled.
    // Invalid/overflowing envelopes are conservatively NOT rejected.
    inline bool mots_swept_sphere_definitely_outside(
        thing_node_vector const &start,thing_node_vector const &direction,
        float length,float moving_radius,thing_node_vector const &center,
        float bound_radius)
    {
        if(!std::isfinite(length) || length<0 ||
           !std::isfinite(moving_radius) || moving_radius<0 ||
           !std::isfinite(bound_radius) || bound_radius<0)
            return false;
        for(float x:start)if(!std::isfinite(x))return false;
        for(float x:direction)if(!std::isfinite(x))return false;
        for(float x:center)if(!std::isfinite(x))return false;
        double const dir2=double(direction[0])*direction[0]+
                          double(direction[1])*direction[1]+
                          double(direction[2])*direction[2];
        if(std::fabs(dir2-1.0)>0.001)return false;
        double const dx=double(center[0])-start[0];
        double const dy=double(center[1])-start[1];
        double const dz=double(center[2])-start[2];
        double const along=dx*direction[0]+dy*direction[1]+
                           dz*direction[2];
        double const t=std::clamp(along/dir2,0.0,double(length));
        double const ox=dx-t*direction[0];
        double const oy=dy-t*direction[1];
        double const oz=dz-t*direction[2];
        double const dist2=ox*ox+oy*oy+oz*oz;
        // Expand the envelope slightly to cover float transforms/rounding.
        double const radius=(double(bound_radius)+moving_radius)*
                            (1.0+1.0e-5)+1.0e-5;
        return std::isfinite(dist2) && std::isfinite(radius) &&
               dist2>radius*radius;
    }

    // Query a prepared model. The supplied pose reader must return matrices
    // for the collision Thing's active nodes, whether or not rendered.
    // Invalid pose/radii/hierarchy does not turn into a collision "miss".
    inline mots_query_status mots_probe_collision_tree(
        mots_native_model_snapshot const &snapshot,
        mots_native_pose_reader const &pose,
        bool fitted_collision_spheres,
        thing_node_vector const &thing_origin,
        thing_node_vector const &start,
        thing_node_vector const &direction,
        float max_distance,float moving_radius,
        mots_collision_query_hit &out,
        mots_collision_query_stats *stats=nullptr)
    {
        if(!std::isfinite(max_distance) || max_distance<0 ||
           !std::isfinite(moving_radius) || moving_radius<0)
            return mots_query_status::unavailable;
        for(float x:start)if(!std::isfinite(x))
            return mots_query_status::unavailable;
        for(float x:direction)if(!std::isfinite(x))
            return mots_query_status::unavailable;
        for(float x:thing_origin)if(!std::isfinite(x))
            return mots_query_status::unavailable;

        // The narrow phase uses a unit vector even for an overlap query.
        if(std::fabs(mots_dot(direction,direction)-1.0f)>0.001f)
            return mots_query_status::unavailable;

        std::vector<mots_tree_node> nodes;
        if(!build_native_collision_tree(snapshot,pose,
                                        fitted_collision_spheres,nodes))
            return mots_query_status::unavailable;

        uint32_t active=0;
        for(size_t i=0;i<nodes.size();++i) {
            if(!nodes[i].has_mesh ||
               !mots_tree_node_active(nodes,uint32_t(i)))continue;
            thing_node_vector center{};
            float radius=0.0f;
            if(!mots_tree_world_sphere(nodes[i],center,radius))
                return mots_query_status::unavailable;
            ++active;
        }

        mots_collision_query_stats metrics{};
        metrics.active_meshes=active;
        float envelope=0.0f;
        if(active>0 && mots_tree_enclosing_radius(nodes,thing_origin,envelope)) {
            metrics.tested_broadphase=true;
            if(mots_swept_sphere_definitely_outside(
                  start,direction,max_distance,moving_radius,
                  thing_origin,envelope)) {
                metrics.rejected_broadphase=true;
                if(stats)*stats=metrics;
                return mots_query_status::miss;
            }
        }

        mots_tree_hit hit{};
        if(!mots_tree_sphere_cast(nodes,start,direction,
                                 max_distance,moving_radius,hit)) {
            if(stats)*stats=metrics;
            return mots_query_status::miss;
        }
        out={hit.node,hit.distance,hit.normal,hit.center,false};
        if(stats)*stats=metrics;
        return mots_query_status::hit;
    }

    // Opt-in substitute for the sphere-tree branch of the original JKGM
    // sithIntersect_CheckSphereThingIntersection. All normal JK collide
    // modes and unsupported raycast flags MUST fall back to native handling.
    //
    // The caller supplies resolved/prevalidated snapshots and matrices for
    // both Things. For reversed casts, select the moving Thing's model and
    // cast backwards from the checked Thing's center, as MotS does.
    inline mots_query_status mots_probe_type2_collision(
        mots_collision_participant const *moving,
        mots_collision_participant const &checked,
        mots_native_model_snapshot const *moving_model,
        mots_native_model_snapshot const *checked_model,
        mots_native_pose_reader const &moving_pose,
        mots_native_pose_reader const &checked_pose,
        thing_node_vector const &moving_position,
        thing_node_vector const &checked_position,
        thing_node_vector const &start,
        thing_node_vector const &unit_direction,
        float length,float moving_radius,
        uint32_t flags,bool type2_enabled,bool fitted_spheres,
        mots_collision_query_hit &out,
        mots_collision_query_stats *stats=nullptr)
    {
        mots_collision_dispatch mode{};
        if(!select_mots_collision(moving,checked,moving_radius,
                                  flags,type2_enabled,mode) ||
           mode.path!=mots_collision_path::sphere_tree)
            return mots_query_status::unavailable;

        // Only the base type-2 branch has been reproduced. Other native
        // flags (including RAYCAST_400) may change the output convention.
        if(flags!=0U)return mots_query_status::unavailable;

        bool const reversed=mode.reverse_cast;
        auto const *model=reversed?moving_model:checked_model;
        auto const &pose=reversed?moving_pose:checked_pose;
        auto const &position=reversed?moving_position:checked_position;
        if(!model)return mots_query_status::unavailable;

        thing_node_vector ray_start=reversed?checked_position:start;
        thing_node_vector ray_dir=reversed
            ? thing_node_vector{{-unit_direction[0],-unit_direction[1],
                                 -unit_direction[2]}}
            : unit_direction;
        mots_collision_query_hit hit{};
        auto const status=mots_probe_collision_tree(
            *model,pose,fitted_spheres,position,ray_start,ray_dir,
            length,mode.narrowphase_moving_radius,hit,stats);
        if(status!=mots_query_status::hit)return status;
        if(reversed)for(float &v:hit.normal)v=-v;
        hit.reverse_cast=reversed;
        out=hit;
        return mots_query_status::hit;
    }
}
