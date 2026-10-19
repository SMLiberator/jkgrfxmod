#pragma once
// Preserve JK's native per-mesh sphere rejection when its CPU draw is bypassed.
// This is NOT the retired AABB/tighter-bounds experiment; it uses the
// existing rdMesh::radius and original rdClipFrustum fields.
#include <algorithm>
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
}
