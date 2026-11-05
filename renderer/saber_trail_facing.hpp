#pragma once
// Camera-oriented rendering of a delayed saber tip. The delayed TIME
// remains physical; only its visual bearing is rotated into the screen
// plane so a depth-heavy swing still produces the cinematic glowing wedge.
// Native rdCamera view coordinates use X/Z as the screen plane, Y as depth.
#include <algorithm>
#include <array>
#include <cmath>
namespace jkgm {
using saber_trail_point=std::array<float,3>;

inline bool saber_trail_facing_tip(
    saber_trail_point const &base,
    saber_trail_point const &live,
    saber_trail_point const &past,
    saber_trail_point &out)
{
    float a[3]{},b[3]{};
    float a2=0,b2=0;
    for(int n=0;n<3;++n) {
        if(!std::isfinite(base[n]) || !std::isfinite(live[n]) ||
           !std::isfinite(past[n]))return false;
        a[n]=live[n]-base[n];
        b[n]=past[n]-base[n];
        a2+=a[n]*a[n];b2+=b[n]*b[n];
    }
    float const screen2=a[0]*a[0]+a[2]*a[2];
    if(a2<1.e-8f || b2<1.e-8f || screen2<1.e-8f)return false;
    float dot=a[0]*b[0]+a[1]*b[1]+a[2]*b[2];
    dot=std::clamp(dot/std::sqrt(a2*b2),-1.0f,1.0f);
    float const angle=std::acos(dot); // 0 when delayed blade catches up
    if(!std::isfinite(angle))return false;

    // A true 3D fan can be almost edge-on, especially during JK's
    // forward/back first-person attacks. Rotate the delayed bearing in
    // the X/Z screen plane by the SAME 3D angular separation.
    float const side=a[0]*b[2]-a[2]*b[0];
    // Near the screen-plane singularity, preserve the direction of the
    // depth-heavy swing instead of arbitrarily collapsing its width.
    float const sign=std::abs(side)>1.e-7f ?
        (side>0.f?1.f:-1.f) : (b[1]<a[1]?1.f:-1.f);
    float const c=std::cos(angle),ss=sign*std::sin(angle);
    out={{
        base[0]+a[0]*c-a[2]*ss,
        live[1], // keep both tip edges at the current blade's view depth
        base[2]+a[0]*ss+a[2]*c
    }};
    return std::isfinite(out[0]) && std::isfinite(out[1]) &&
           std::isfinite(out[2]);
}
} // namespace jkgm
