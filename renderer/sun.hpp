#pragma once
#include "animated.hpp"
#include <array>
#include <cmath>

namespace jkgm {
    // World-space direction TOWARD the sun, JK X/Y horizontal and Z up.
    // ResetSun and new sessions default to sun intensity zero.
    struct sun_settings {
        animated_floats<3> direction{std::array<float, 3>{0.0f, 0.0f, 1.0f}};
        animated_floats<3> color{std::array<float, 3>{1.0f, 1.0f, 1.0f}};
        animated_floats<1> intensity{std::array<float, 1>{0.0f}};
    };
    inline sun_settings &get_sun_settings()
    {
        static sun_settings s;
        return s;
    }
    inline std::array<float, 3> normalized_sun_direction(std::array<float, 3> d)
    {
        float const sq = d[0]*d[0] + d[1]*d[1] + d[2]*d[2];
        if(!(sq > 1.0e-10f) || !std::isfinite(sq)) {
            return {0.0f, 0.0f, 1.0f};
        }
        float const inv = 1.0f / std::sqrt(sq);
        return {d[0]*inv, d[1]*inv, d[2]*inv};
    }
}
