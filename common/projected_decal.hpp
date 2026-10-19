#pragma once
// First experimental orthographic, depth-projected decal. This is not a
// material alpha mode: it projects independently onto the shared opaque scene.
#include <algorithm>
#include <array>
#include <cmath>
#include <string>

namespace jkgm {
    enum class projected_decal_blend { alpha=0, multiply=1, additive=2 };
    struct projected_decal_config {
        std::string image;
        std::array<float,3> position{{0,0,0}};
        std::array<float,3> direction{{0,0,-1}};
        std::array<float,3> up{{0,1,0}};
        // Width and height centered at position; depth extends along direction.
        std::array<float,3> size{{4,4,1}};
        float opacity=1.0f;
        float normal_threshold=0.25f;
        projected_decal_blend blend=projected_decal_blend::multiply;
    };
    struct projected_decal_basis {
        std::array<float,3> right{},up{},forward{};
    };
    inline bool prepare_projected_decal(projected_decal_config const &d,
                                        projected_decal_basis &basis) {
        if(d.image.empty()) return false;
        for(float a:d.position) if(!std::isfinite(a)) return false;
        for(float a:d.size) if(!std::isfinite(a) || a<=0.0f) return false;
        if(!std::isfinite(d.opacity) || d.opacity<0 || d.opacity>1 ||
           !std::isfinite(d.normal_threshold) ||
           d.normal_threshold<0 || d.normal_threshold>1)
            return false;
        for(float a:d.direction) if(!std::isfinite(a)) return false;
        for(float a:d.up) if(!std::isfinite(a)) return false;
        auto dot=[](std::array<float,3> const &a,
                    std::array<float,3> const &b) {
            return a[0]*b[0]+a[1]*b[1]+a[2]*b[2];
        };
        auto normalize=[&](std::array<float,3> &a) {
            float const len=std::sqrt(dot(a,a));
            if(!std::isfinite(len)||len<1e-5f) return false;
            for(float &v:a) v/=len;
            return true;
        };
        basis.forward=d.direction;
        if(!normalize(basis.forward)) return false;
        basis.up=d.up;
        float const along=dot(basis.up,basis.forward);
        for(int i=0;i<3;i++) basis.up[i]-=along*basis.forward[i];
        if(!normalize(basis.up)) return false;
        auto const &f=basis.forward,&u=basis.up;
        basis.right={{f[1]*u[2]-f[2]*u[1],
                      f[2]*u[0]-f[0]*u[2],
                      f[0]*u[1]-f[1]*u[0]}};
        if(!normalize(basis.right)) return false;
        return true;
    }
}
