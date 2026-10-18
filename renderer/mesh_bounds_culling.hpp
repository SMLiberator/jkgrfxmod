#pragma once
// Conservative camera-frustum test for a locally axis-aligned mesh bounds.
// Does NOT perform occlusion culling or reject geometry hidden behind walls.
// Camera-space forward is +Y, right +X, up +Z in the Sith engine.
#include <cmath>

namespace jkgm {
namespace mesh_bounds {
    struct vec3 { float x, y, z; };
    struct matrix34 { vec3 right, forward, up, offset; };
    struct frustum {
        int clip_far;
        float near_plane, far_plane;
        float ortho_left, ortho_top, ortho_right, ortho_bottom;
        float top, bottom, left, right, near_top, near_left;
    };
    struct bounds { vec3 center, extent; };
    static_assert(sizeof(vec3) == 12, "Sith vector layout");
    static_assert(sizeof(matrix34) == 48, "Sith matrix layout");
    static_assert(sizeof(frustum) == 52, "Sith frustum layout");

    inline bool finite(float f) { return std::isfinite(f); }
    inline bool valid(bounds const &b)
    {
        return finite(b.center.x) && finite(b.center.y) && finite(b.center.z) &&
               finite(b.extent.x) && finite(b.extent.y) && finite(b.extent.z) &&
               b.extent.x >= 0 && b.extent.y >= 0 && b.extent.z >= 0;
    }
    inline bool valid(frustum const &f)
    {
        return finite(f.near_plane) && finite(f.far_plane) &&
               finite(f.left) && finite(f.right) && finite(f.top) &&
               finite(f.bottom) && f.near_plane > 0 &&
               f.left < f.right && f.bottom < f.top &&
               (!f.clip_far || f.far_plane > f.near_plane);
    }

    // Largest possible value of a linear frustum half-space on the AABB.
    // If that maximum is outside, EVERY point of the AABB is outside.
    inline float max_plane(bounds const &b, matrix34 const &m,
                           float px, float py, float pz, float constant)
    {
        float const ax = px*m.right.x + py*m.right.y + pz*m.right.z;
        float const ay = px*m.forward.x + py*m.forward.y + pz*m.forward.z;
        float const az = px*m.up.x + py*m.up.y + pz*m.up.z;
        float const at = px*m.offset.x + py*m.offset.y + pz*m.offset.z + constant;
        return at + ax*b.center.x + ay*b.center.y + az*b.center.z +
               std::fabs(ax)*b.extent.x +
               std::fabs(ay)*b.extent.y +
               std::fabs(az)*b.extent.z;
    }

    // All tests include positive slack, so borderline meshes stay visible.
    // Incorrect/missing camera data must fail open (return false).
    inline bool definitely_outside(bounds const &b, matrix34 const &model_view,
                                   frustum const &f, int projection_type)
    {
        if(projection_type != 1 || !valid(b) || !valid(f)) return false;
        auto const &m = model_view;
        float const s = 0.05f; // conservative near-boundary tolerance in JK units
        if(max_plane(b,m,0, 1,0,-f.near_plane) < -s) return true;
        if(f.clip_far && max_plane(b,m,0,-1,0, f.far_plane) < -s) return true;
        if(max_plane(b,m, 1,-f.left,0,0) < -s) return true;  // all x < left*y
        if(max_plane(b,m,-1, f.right,0,0) < -s) return true; // all x > right*y
        if(max_plane(b,m,0,-f.bottom, 1,0) < -s) return true; // all z < bottom*y
        if(max_plane(b,m,0, f.top,-1,0) < -s) return true;    // all z > top*y
        return false;
    }
}
}
