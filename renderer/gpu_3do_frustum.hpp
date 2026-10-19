#pragma once
// Preserve JK's native per-mesh sphere rejection when its CPU draw is bypassed.
// This is NOT the retired AABB/tighter-bounds experiment; it uses the
// existing rdMesh::radius and original rdClipFrustum fields.
#include "gpu_3do_modelview.hpp"
#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>

namespace jkgm {
    struct gpu_3do_native_frustum {
        int32_t clip_far;
        float near_plane,far_plane;
        float ortho_left,ortho_top,ortho_right,ortho_bottom;
        float far_top,bottom,far_left,right,near_top,near_left;
    };
    static_assert(sizeof(gpu_3do_native_frustum)==52,
                  "Unexpected native rdClipFrustum size");
    inline bool gpu_3do_sphere_outside(gpu_3do_native_frustum const &f,
                                       bool perspective,
                                       float x,float y,float z,float radius)
    {
        if(!std::isfinite(x)||!std::isfinite(y)||!std::isfinite(z)||
           !std::isfinite(radius)||radius<0.0f ||
           !std::isfinite(f.near_plane)||!std::isfinite(f.far_plane)||
           !std::isfinite(f.far_top)||!std::isfinite(f.bottom)||
           !std::isfinite(f.far_left)||!std::isfinite(f.right))
            return false; // never incorrectly reject on invalid data
        // Match rdClip_SphereInFrustrum's *outside* inequalities only.
        float const lo=y-radius,hi=y+radius;
        if(hi<f.near_plane) return true;
        if(f.clip_far && lo>f.far_plane) return true;
        float const top_a=perspective ? f.far_top*lo : f.ortho_top;
        float const top_b=perspective ? f.far_top*hi : f.ortho_top;
        if(z-radius>top_a && z-radius>top_b) return true;
        float const bot_a=perspective ? f.bottom*lo : f.ortho_bottom;
        float const bot_b=perspective ? f.bottom*hi : f.ortho_bottom;
        if(z+radius<bot_a && z+radius<bot_b) return true;
        float const left_a=perspective ? f.far_left*lo : f.ortho_left;
        float const left_b=perspective ? f.far_left*hi : f.ortho_left;
        if(x+radius<left_a && x+radius<left_b) return true;
        float const right_a=perspective ? f.right*lo : f.ortho_right;
        float const right_b=perspective ? f.right*hi : f.ortho_right;
        return x-radius>right_a && x-radius>right_b;
    }

    // Conservative native-frustum test for a *batch* of faces in one mesh.
    // Geometry stays GPU resident: transform the six AABB extents with the
    // composed model-view matrix and test their support against six planes.
    // Reject only when the entire batch lies outside the same plane.
    // Native rdPrimit3_ClipFace does this more finely, per polygon.
    inline bool gpu_3do_batch_outside_frustum(
        gpu_3do_native_frustum const &f,
        gpu_3do_matrix34 const &m,
        std::array<float,3> const &lo,
        std::array<float,3> const &hi)
    {
        if(!std::isfinite(f.near_plane)||!std::isfinite(f.far_plane)||
           !std::isfinite(f.far_left)||!std::isfinite(f.right)||
           !std::isfinite(f.far_top)||!std::isfinite(f.bottom))
            return false;
        std::array<float,3> center{},extent{};
        for(int i=0;i<3;i++) {
            if(!std::isfinite(lo[i])||!std::isfinite(hi[i])||lo[i]>hi[i])
                return false;
            center[i]=0.5f*(lo[i]+hi[i]);
            extent[i]=0.5f*(hi[i]-lo[i]);
        }
        auto const v=gpu_3do_native_transform_point(m,center);
        float const ex=std::fabs(m[0])*extent[0]+
                       std::fabs(m[3])*extent[1]+std::fabs(m[6])*extent[2];
        float const ey=std::fabs(m[1])*extent[0]+
                       std::fabs(m[4])*extent[1]+std::fabs(m[7])*extent[2];
        float const ez=std::fabs(m[2])*extent[0]+
                       std::fabs(m[5])*extent[1]+std::fabs(m[8])*extent[2];
        if(!std::isfinite(v[0])||!std::isfinite(v[1])||
           !std::isfinite(v[2])||!std::isfinite(ex)||
           !std::isfinite(ey)||!std::isfinite(ez))
            return false;
        if(v[1]+ey<f.near_plane) return true;
        if(f.clip_far && v[1]-ey>f.far_plane) return true;
        if(v[0]-f.far_left*v[1]+ex+std::fabs(f.far_left)*ey<0)
            return true;
        if(v[0]-f.right*v[1]-ex-std::fabs(f.right)*ey>0)
            return true;
        if(v[2]-f.far_top*v[1]-ez-std::fabs(f.far_top)*ey>0)
            return true;
        return v[2]-f.bottom*v[1]+ez+std::fabs(f.bottom)*ey<0;
    }
}
