#pragma once
// Geometry-cache pose tolerance. The old byte-exact matrix comparison
// rebuilt an attached decal every frame on tiny physics/animation jitter.
// A planted blood splat is fixed at its first attached orientation and
// only refits for meaningful displacement, new face normal, or new SPR.
#include <array>
#include <cmath>
#include <cstdint>

namespace jkgm {
inline bool sprite_decal_pose_stable(
    std::array<float,12> const &old_m,
    std::array<float,12> const &new_m,bool attached)
{
    // A fixed face decal should NOT follow small animation/physics
    // rotations every render frame. Keep its initial tangent rotation.
    float const max_movement=attached?0.02f:0.001f;
    for(int i=9;i<12;++i)
        if(!std::isfinite(old_m[i]) || !std::isfinite(new_m[i]) ||
           std::fabs(old_m[i]-new_m[i])>max_movement)return false;
    if(attached)return true;
    for(int i=0;i<9;++i)
        if(!std::isfinite(old_m[i]) || !std::isfinite(new_m[i]) ||
           std::fabs(old_m[i]-new_m[i])>0.001f)return false;
    return true;
}

inline bool sprite_decal_normals_stable(
    std::array<float,3> const &a,std::array<float,3> const &b)
{
    for(int i=0;i<3;++i)
        if(!std::isfinite(a[i])||!std::isfinite(b[i])||
           std::fabs(a[i]-b[i])>0.002f)return false;
    return true;
}
}
