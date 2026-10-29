#pragma once
// Per-Thing, one-shot moving-decal depth fit. A model may be made of several
// independent animated meshes offset along the incoming shot direction.
// A decal centered on the mesh-ray hit can miss an adjacent mesh entirely
// even when both lie inside the blood sprite's projected width and height.
// Fit the SHALLOWEST bounded depth envelope that covers front-facing pieces.
// Do not affect static world decals or change the native SPR Thing placement.
#include "sprite_decal_world_cache.hpp"
#include <algorithm>
#include <cmath>
#include <vector>

namespace jkgm {
inline bool fit_moving_decal_multimesh_depth(
    decal_projector &projector,
    std::vector<decal_world_geometry> const &pieces,
    float maximum_depth=0.25f)
{
    if(pieces.size()<2 || !valid_decal_projector(projector) ||
       !std::isfinite(maximum_depth) ||
       maximum_depth<projector.depth)return false;
    // Probe with a bounded deeper box ONLY for receiver discovery.
    // The final volume remains as shallow as the actual adjacent geometry
    // allows; this does not increase decal depth for single-piece models.
    decal_projector probe=projector;
    probe.depth=maximum_depth;
    for(int axis=0;axis<3;++axis)
        probe.origin[axis]-=probe.look[axis]*
                             (probe.depth-projector.depth)*0.5f;
    float low=0.0f,high=0.0f;
    bool found=false;
    decal_vec3 center{};
    for(int axis=0;axis<3;++axis)
        center[axis]=projector.origin[axis]+projector.look[axis]*
            projector.depth*0.5f;
    decal_volume_aabb const bounds=sprite_decal_volume_aabb(probe);
    for(auto const &piece:pieces) {
        if(!piece.valid_world)continue;
        for(auto const &face:piece.receivers) {
            if(!sprite_decal_bounds_overlap(bounds,face) ||
               !decal_finite(face.normal))continue;
            float const n2=decal_dot(face.normal,face.normal);
            if(n2<1.e-12f || !std::isfinite(n2))continue;
            // Use the authored receiver normal, not regenerated triangle
            // winding. Rear sides of thin body pieces must never increase
            // decal projection depth toward the opposite shell.
            if(-decal_dot(face.normal,projector.look)/std::sqrt(n2)<
               -0.20f)continue;
            auto clipped=clip_projected_decal(
                probe,face.polygon,face.normal,0.05f,false,true);
            if(clipped.size()<3)continue;
            for(auto const &v:clipped) {
                float const relative=decal_dot(
                    decal_sub(v.world,center),projector.look);
                if(!std::isfinite(relative))continue;
                low=std::min(low,relative);
                high=std::max(high,relative);
                found=true;
            }
        }
    }
    if(!found)return false;
    // Keep enough breathing room for the existing fragment depth-feather.
    float const padding=std::max(0.01f,projector.depth*0.15f);
    if(low>=-projector.depth*0.5f+padding &&
       high<=projector.depth*0.5f-padding)return false;
    float const wanted=high-low+2.0f*padding;
    float const actual=std::max(projector.depth,
                                std::min(maximum_depth,wanted));
    // Shifting the slab toward an adjacent offset mesh can be enough even
    // when its original depth is already adequate. Keep the actual impact
    // inside the slab's feathered interior, so it never disappears.
    float const room=std::max(0.0f,actual*0.5f-padding);
    float const shift=std::clamp((low+high)*0.5f,-room,room);
    if(std::fabs(shift)<1.e-6f && actual<=projector.depth)return false;
    for(int axis=0;axis<3;++axis)
        projector.origin[axis]+=projector.look[axis]*
            (shift-(actual-projector.depth)*0.5f);
    projector.depth=actual;
    return true;
}
}
