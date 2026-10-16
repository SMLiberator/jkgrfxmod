#pragma once

#include "animated.hpp"
#include <array>

namespace jkgm {
    // Distance fog state.
    //
    // Written by the fog COG verbs (see main.cpp) and read by the game render passes
    // (see renderer.cpp), which hand it to the game pass shaders as the uniforms
    // `fog_params` and `fog_color`.
    //
    // The defaults below are what a level gets if its COG scripts never touch fog.
    // Fog has zero opacity unless explicitly set by a level script.
    //
    // Color and info can be faded; call .value() to read what should be showing right now.
    struct fog_settings {
        // r, g, b: 0.0 - 1.0, handed to the shader unchanged
        animated_floats<3> color{std::array<float, 3>{0.1f, 0.1f, 0.1f}};

        // start distance, end distance (full strength), opacity (0.0 - 1.0 ceiling for the fog amount)
        animated_floats<3> info{std::array<float, 3>{40.0f, 1000.0f, 0.0f}};
    };

    inline fog_settings &get_fog_settings()
    {
        static fog_settings settings;
        return settings;
    }
}
