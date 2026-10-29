#pragma once
// Temporary world-space receiver snapshot for one visible GPU-cached 3DO
// hierarchy mesh. Only built when a sprite is first projected; never built
// for steady-state moving-decal rendering.
#include "sprite_decal_static_mesh.hpp"
#include "gpu_3do_preview.hpp"
#include <algorithm>
#include <cmath>
#include <vector>

namespace jkgm {
inline bool sprite_decal_moving_receiver_near(
    gpu_3do_instance const &inst,decal_vec3 const &world_point,
    float distance=0.5f)
{
    if(!inst.geometry || !gpu_3do_matrix_finite(inst.transform) ||
       !decal_finite(world_point))return false;
    float local_max2=0.0f;
    for(auto const &g:inst.geometry->groups) {
        if(g.count<3 || (g.face_type&0x2U))continue;
        for(int c=0;c<8;++c) {
            float const x=(c&1)?g.bounds_max[0]:g.bounds_min[0];
            float const y=(c&2)?g.bounds_max[1]:g.bounds_min[1];
            float const z=(c&4)?g.bounds_max[2]:g.bounds_min[2];
            local_max2=std::max(local_max2,x*x+y*y+z*z);
        }
    }
    float max_axis=0.0f;
    for(int i=0;i<9;i+=3) {
        float const len=std::sqrt(
            inst.transform[i]*inst.transform[i]+
            inst.transform[i+1]*inst.transform[i+1]+
            inst.transform[i+2]*inst.transform[i+2]);
        max_axis=std::max(max_axis,len);
    }
    // Conservative even with sheared axes (sum of the three lengths).
    float const radius=std::sqrt(local_max2)*max_axis*1.733f+distance;
    float delta2=0;
    for(int axis=0;axis<3;++axis) {
        float const delta=world_point[axis]-inst.transform[9+axis];
        delta2+=delta*delta;
    }
    return std::isfinite(radius) && std::isfinite(delta2) &&
        radius>distance && delta2<=radius*radius;
}

inline decal_world_geometry sprite_decal_moving_receiver_world(
    gpu_3do_instance const &inst,uint32_t max_triangles=8192U)
{
    decal_world_geometry world{};
    if(!inst.geometry || !gpu_3do_matrix_finite(inst.transform))return world;
    world.valid_world=true;
    auto const &geometry=*inst.geometry;
    // The geometry's position indices are already triangulated and have
    // exactly the model vertex positions the GPU path will draw.
    for(auto const &group:geometry.groups) {
        if(group.count<3 || (group.face_type&0x2U) ||
           group.offset>geometry.indices.size() ||
           group.count>geometry.indices.size()-group.offset)continue;
        for(uint32_t n=0;n+2<group.count;n+=3) {
            if(world.receivers.size()>=max_triangles) {
                world.valid_world=false;
                world.receivers.clear();
                return world;
            }
            std::vector<decal_vec3> polygon;
            polygon.reserve(3);
            bool valid=true;
            for(uint32_t k=0;k<3;++k) {
                uint32_t const idx=geometry.indices[group.offset+n+k];
                if(idx>=geometry.vertices.size()) {
                    valid=false;break;
                }
                auto const &v=geometry.vertices[idx];
                polygon.push_back({{v.x,v.y,v.z}});
            }
            if(!valid)continue;
            decal_world_surface_geometry receiver{};
            if(build_static_sprite_receiver(
                   inst.transform,polygon,
                   static_cast<uint32_t>(world.receivers.size()),receiver))
                world.receivers.push_back(std::move(receiver));
        }
    }
    world.surface_count=static_cast<uint32_t>(world.receivers.size());
    return world;
}
}
