#pragma once
// World XYZ <-> Thing-local RLU point transforms.
// The JKGM native SithThing layout stores its 3x3 basis at +0x88 and
// position at +0xb8. This pure helper inverts the ACTUAL basis, including
// non-unit, mirrored and sheared R/L/U vectors; dot products only work for
// orthonormal bases. No assumption about PYR or a separate animation clock.
#include <array>
#include <cmath>

namespace jkgm {
using cog_thing_offset_vec=std::array<float,3>;
using cog_thing_offset_basis=std::array<float,9>;

inline bool cog_thing_pos_offset(
    cog_thing_offset_basis const &basis,cog_thing_offset_vec const &origin,
    cog_thing_offset_vec const &world,cog_thing_offset_vec &local)
{
    double r[3]{},l[3]{},u[3]{},delta[3]{};
    double rr=0.0,ll=0.0,uu=0.0;
    for(int i=0;i<3;++i) {
        if(!std::isfinite(basis[i]) ||
           !std::isfinite(basis[i+3]) ||
           !std::isfinite(basis[i+6]) ||
           !std::isfinite(origin[i]) ||
           !std::isfinite(world[i]))return false;
        r[i]=basis[i];l[i]=basis[i+3];u[i]=basis[i+6];
        delta[i]=double(world[i])-double(origin[i]);
        rr+=r[i]*r[i];ll+=l[i]*l[i];uu+=u[i]*u[i];
    }
    auto const cross=[](double const *a,double const *b) {
        return std::array<double,3>{{
            a[1]*b[2]-a[2]*b[1],
            a[2]*b[0]-a[0]*b[2],
            a[0]*b[1]-a[1]*b[0]
        }};
    };
    auto const dot=[](double const *a,double const *b) {
        return a[0]*b[0]+a[1]*b[1]+a[2]*b[2];
    };
    auto const lu=cross(l,u),ur=cross(u,r),rl=cross(r,l);
    double const det=dot(r,lu.data());
    double const scale=std::sqrt(rr)*std::sqrt(ll)*std::sqrt(uu);
    // Reject degenerate or nearly singular transforms rather than
    // returning huge/infinite local coordinates to the COG interpreter.
    if(!(scale>0.0) || !std::isfinite(scale) ||
       !std::isfinite(det) || std::fabs(det)<=scale*1.e-9)return false;
    double const inv=1.0/det;
    double const result[3]{
        dot(delta,lu.data())*inv,
        dot(delta,ur.data())*inv,
        dot(delta,rl.data())*inv
    };
    for(double v:result)
        if(!std::isfinite(v) || std::fabs(v)>1.e10)return false;
    local={{float(result[0]),float(result[1]),float(result[2])}};
    return std::isfinite(local[0]) && std::isfinite(local[1]) &&
           std::isfinite(local[2]);
}

// Inverse of GetThingPosOffset: apply the actual SithThing axes
// (including scale, mirror and shear), then its world-space position.
inline bool cog_thing_offset_pos(
    cog_thing_offset_basis const &basis,cog_thing_offset_vec const &origin,
    cog_thing_offset_vec const &local,cog_thing_offset_vec &world)
{
    for(float v:basis)if(!std::isfinite(v))return false;
    for(float v:origin)if(!std::isfinite(v))return false;
    for(float v:local)if(!std::isfinite(v))return false;
    for(int axis=0;axis<3;++axis) {
        double const result=double(origin[axis])+
            double(basis[axis])*double(local[0])+
            double(basis[axis+3])*double(local[1])+
            double(basis[axis+6])*double(local[2]);
        if(!std::isfinite(result) || std::fabs(result)>1.e10)
            return false;
        world[axis]=float(result);
    }
    return true;
}
} // namespace jkgm
