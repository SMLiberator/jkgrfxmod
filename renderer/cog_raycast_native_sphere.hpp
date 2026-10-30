#pragma once
// RayCastEx collide=3 broadphase: reuse the same SithThing::position and
// SithThing::collideSize sphere that JK uses to select collision candidates.
// Its intentionally conservative false negatives are acceptable: the
// precise triangle test only exists to eliminate sphere false positives.
// This helper does NOT alter gameplay collision/physics.
#include <algorithm>
#include <array>
#include <cmath>

namespace jkgm {
inline bool cog_raycast_native_sphere_candidate(
    std::array<float,3> const &start,
    std::array<float,3> const &direction,
    float length,
    std::array<float,3> const &center,
    float collide_size)
{
    if(!(collide_size>0.0f) || !std::isfinite(collide_size) ||
       !(length>0.0f) || !std::isfinite(length))
        return false;
    for(int i=0;i<3;++i)
        if(!std::isfinite(start[i]) ||
           !std::isfinite(direction[i]) ||
           !std::isfinite(center[i]))
            return false;
    // The direction supplied by RayCastEx is normalized. Compute the
    // nearest point on the FINITE ray segment to the native collision sphere.
    // A sphere behind the origin must not trigger a costly mesh test.
    double along=0.0;
    for(int i=0;i<3;++i)
        along+=(double(center[i])-start[i])*direction[i];
    along=std::clamp(along,0.0,double(length));
    double distance_squared=0.0;
    for(int i=0;i<3;++i) {
        double const delta=double(center[i])-start[i]-
                           along*direction[i];
        distance_squared+=delta*delta;
    }
    double const radius=collide_size;
    return distance_squared<=radius*radius;
}
} // namespace jkgm
