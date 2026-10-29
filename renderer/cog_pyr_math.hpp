#pragma once
// JK Sith Engine PYR: degrees in Pitch/Yaw/Roll order (X/Y/Z).
// +X = Right, +Y = Look, +Z = Up. The matrix equations below
// reproduce rdMatrix_Build34 without executable-specific native calls.
#include <array>
#include <cmath>

namespace jkgm {
using cog_pyr_vec=std::array<float,3>;
enum class cog_pyr_axis {right,look,up};
struct cog_pyr_basis {
    cog_pyr_vec right{},look{},up{};
};

inline float cog_pyr_dot(cog_pyr_vec const &a,cog_pyr_vec const &b)
{
    return a[0]*b[0]+a[1]*b[1]+a[2]*b[2];
}
inline cog_pyr_vec cog_pyr_cross(cog_pyr_vec const &a,cog_pyr_vec const &b)
{
    return {{a[1]*b[2]-a[2]*b[1],
             a[2]*b[0]-a[0]*b[2],
             a[0]*b[1]-a[1]*b[0]}};
}
inline bool cog_pyr_normalize(cog_pyr_vec &v)
{
    double squared=0.0;
    for(float value:v) {
        if(!std::isfinite(value))return false;
        squared+=double(value)*value;
    }
    if(!(squared>1.e-24) || !std::isfinite(squared))return false;
    float const inv=float(1.0/std::sqrt(squared));
    for(float &value:v)value*=inv;
    return true;
}
inline cog_pyr_vec cog_pyr_project(cog_pyr_vec reference,
                                    cog_pyr_vec const &unit)
{
    float const scale=cog_pyr_dot(reference,unit);
    for(int i=0;i<3;++i)reference[i]-=scale*unit[i];
    return reference;
}

// The native rdMatrix_Build34 equations with zero translation.
inline bool cog_pyr_build(cog_pyr_vec pyr,cog_pyr_basis &basis)
{
    constexpr double radians=0.017453292519943295769;
    for(float &angle:pyr) {
        if(!std::isfinite(angle))return false;
        angle=std::remainder(angle,360.0f);
    }
    double const p=double(pyr[0])*radians;
    double const y=double(pyr[1])*radians;
    double const r=double(pyr[2])*radians;
    double const sp=std::sin(p),cp=std::cos(p);
    double const sy=std::sin(y),cy=std::cos(y);
    double const sr=std::sin(r),cr=std::cos(r);
    basis.right={{float(-sr*sy*sp+cr*cy),
                  float(sr*cy*sp+cr*sy),float(-sr*cp)}};
    basis.look={{float(-sy*cp),float(cy*cp),float(sp)}};
    basis.up={{float(cr*sy*sp+sr*cy),
               float(-sp*cy*cr+sy*sr),float(cr*cp)}};
    return true;
}

inline cog_pyr_vec cog_pyr_direction(cog_pyr_vec pyr,cog_pyr_axis axis)
{
    cog_pyr_basis basis{};
    if(!cog_pyr_build(pyr,basis))return {};
    switch(axis) {
        case cog_pyr_axis::right:return basis.right;
        case cog_pyr_axis::look:return basis.look;
        case cog_pyr_axis::up:return basis.up;
    }
    return {};
}

// Extract from a complete orthonormal basis. At +/-90 pitch yaw and
// roll are not independent; choose yaw=0 and retain the orientation
// in roll instead.
inline cog_pyr_vec cog_pyr_extract(cog_pyr_basis const &basis)
{
    constexpr double degrees=57.295779513082320877;
    double const horizontal=std::hypot(double(basis.look[0]),
                                        double(basis.look[1]));
    double const pitch=std::atan2(double(basis.look[2]),horizontal);
    double yaw=0.0,roll=0.0;
    if(horizontal>1.e-6) {
        yaw=std::atan2(-double(basis.look[0]),
                        double(basis.look[1]));
        roll=std::atan2(-double(basis.right[2]),
                         double(basis.up[2]));
    } else {
        roll=std::atan2((pitch>=0.0?1.0:-1.0)*
                        double(basis.right[1]),
                        double(basis.right[0]));
    }
    return {{float(pitch*degrees),float(yaw*degrees),float(roll*degrees)}};
}

// The input direction is normalized. An axis alone cannot determine
// rotation about itself. Right prefers world Up, Up prefers world Look,
// and Look always yields roll=0, matching SetThingLook's semantics.
inline cog_pyr_vec cog_pyr_from_direction(cog_pyr_vec direction,
                                          cog_pyr_axis axis)
{
    if(!cog_pyr_normalize(direction))return {};
    constexpr double degrees=57.295779513082320877;
    if(axis==cog_pyr_axis::look) {
        double const h=std::hypot(double(direction[0]),
                                  double(direction[1]));
        return {{float(std::atan2(double(direction[2]),h)*degrees),
                 h>1.e-6 ? float(std::atan2(-double(direction[0]),
                                            double(direction[1]))*degrees)
                         : 0.0f,0.0f}};
    }
    cog_pyr_basis basis{};
    if(axis==cog_pyr_axis::right) {
        basis.right=direction;
        basis.up=cog_pyr_project({{0,0,1}},direction);
        if(!cog_pyr_normalize(basis.up)) {
            basis.up=cog_pyr_project({{0,1,0}},direction);
            if(!cog_pyr_normalize(basis.up))return {};
        }
        basis.look=cog_pyr_cross(basis.up,basis.right);
        if(!cog_pyr_normalize(basis.look))return {};
        basis.up=cog_pyr_cross(basis.right,basis.look);
    } else {
        basis.up=direction;
        basis.look=cog_pyr_project({{0,1,0}},direction);
        if(!cog_pyr_normalize(basis.look)) {
            basis.look=cog_pyr_project({{1,0,0}},direction);
            if(!cog_pyr_normalize(basis.look))return {};
        }
        basis.right=cog_pyr_cross(basis.look,basis.up);
        if(!cog_pyr_normalize(basis.right))return {};
        basis.look=cog_pyr_cross(basis.up,basis.right);
    }
    return cog_pyr_extract(basis);
}
} // namespace jkgm
