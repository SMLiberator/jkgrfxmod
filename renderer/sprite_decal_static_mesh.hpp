#pragma once
// Project stationary model faces into world-space decal receivers. The input
// matrix is the native rdModel3_DrawMesh joint matrix, not a camera matrix.
// Keep this geometry-only helper independent of JK pointers and GPU state.
#include "sprite_decal_world_cache.hpp"
#include "gpu_3do_modelview.hpp"
#include <cmath>
#include <limits>
#include <vector>

namespace jkgm {
inline bool build_static_sprite_receiver(
    gpu_3do_matrix34 const &world_matrix,
    std::vector<decal_vec3> const &local_polygon,
    uint32_t surface_index,
    decal_world_surface_geometry &result)
{
    if(!gpu_3do_matrix_finite(world_matrix) ||
       local_polygon.size()<3 || local_polygon.size()>128)return false;
    decal_world_surface_geometry receiver{};
    receiver.surface_index=surface_index;
    receiver.bounds_min={{std::numeric_limits<float>::max(),
                          std::numeric_limits<float>::max(),
                          std::numeric_limits<float>::max()}};
    receiver.bounds_max={{-std::numeric_limits<float>::max(),
                          -std::numeric_limits<float>::max(),
                          -std::numeric_limits<float>::max()}};
    receiver.polygon.reserve(local_polygon.size());
    for(auto const &p:local_polygon) {
        if(!decal_finite(p))return false;
        auto world=gpu_3do_native_transform_point(world_matrix,p);
        if(!decal_finite(world))return false;
        for(int i=0;i<3;++i) {
            receiver.bounds_min[i]=std::min(receiver.bounds_min[i],world[i]);
            receiver.bounds_max[i]=std::max(receiver.bounds_max[i],world[i]);
        }
        receiver.polygon.push_back(world);
    }
    auto const &first=receiver.polygon[0];
    bool found=false;
    for(size_t i=1;i+1<receiver.polygon.size();++i) {
        auto const a=decal_sub(receiver.polygon[i],first);
        auto const b=decal_sub(receiver.polygon[i+1],first);
        decal_vec3 cross{{
            a[1]*b[2]-a[2]*b[1],
            a[2]*b[0]-a[0]*b[2],
            a[0]*b[1]-a[1]*b[0]}};
        float const len2=decal_dot(cross,cross);
        if(!std::isfinite(len2))return false;
        if(len2>1.0e-12f) {
            float const inv=1.0f/std::sqrt(len2);
            for(int j=0;j<3;++j)cross[j]*=inv;
            receiver.normal=cross;
            found=true;
            break;
        }
    }
    if(!found)return false;
    result=std::move(receiver);
    return true;
}
}
