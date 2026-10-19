#pragma once
#include <array>
#include <cmath>
namespace jkgm {
    inline std::array<float,3> gpu_3do_cmp_tint(bool identity,
                                                 std::array<float,3> rgb) {
        if(identity) return {{1.0f,1.0f,1.0f}};
        for(float c:rgb) if(!std::isfinite(c)||c<0||c>16)
            return {{1.0f,1.0f,1.0f}};
        return rgb;
    }
}
