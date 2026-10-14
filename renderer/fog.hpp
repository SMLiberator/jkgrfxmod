#pragma once

namespace jkgm {
    // Distance fog state.
    //
    // Written by the fog COG verbs (see main.cpp) and read by the game render passes
    // (see renderer.cpp), which hand it to the game pass shaders as the uniforms
    // `fog_params` and `fog_color`.
    //
    // The defaults below are what a level gets if its COG scripts never touch fog.
    // Fog starts disabled so that levels without fog scripts are unaffected.
    struct fog_settings {
        bool enabled = false;
        float color_r = 0.45f; // 0.0 - 1.0, handed to the shader unchanged
        float color_g = 0.50f;
        float color_b = 0.58f;
        float start = 32.0f;   // distance where fog begins
        float end = 512.0f;    // distance where fog reaches full strength
        float max_opacity = 1.0f; // 0.0 - 1.0, ceiling for the fog amount
    };

    inline fog_settings &get_fog_settings()
    {
        static fog_settings settings;
        return settings;
    }
}
