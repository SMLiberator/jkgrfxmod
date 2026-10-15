#pragma once

#include "animated.hpp"
#include "shaders.hpp"
#include <algorithm>
#include <array>
#include <cmath>

namespace jkgm {
    // Post-processing state, controlled by COG verbs (see main.cpp) and pushed into the final
    // (post_to_srgb) pass once per frame by apply_post_fx().
    //
    // To add another fadeable effect: add an animated_floats member here, a uniform and setter
    // on post_to_srgb_shader, a line in apply_post_fx(), and a pair of verbs in main.cpp.

    struct film_grain_settings {
        // amount: 0.0 - 1.0 (0 = off; 0.05 - 0.15 is a typical range)
        // size:   grain size in screen pixels (1 = finest)
        // rate:   how many times per second the grain pattern changes (0 = frozen)
        animated_floats<3> params{std::array<float, 3>{0.0f, 2.0f, 24.0f}};
    };

    struct vignette_settings {
        // r, g, b: 0.0 - 1.0, the color the screen edges are pulled towards
        // strength: 0.0 - 1.0 (0 = off)
        //
        // The defaults reproduce the original fixed vignette: black edges at full strength.
        animated_floats<4> params{std::array<float, 4>{0.0f, 0.0f, 0.0f, 1.0f}};
    };

    struct color_filter_settings {
        // brightness: -1.0 - 1.0, contrast: 0.0 - 4.0, saturation: 0.0 - 4.0
        //
        // The defaults are the values that used to be hard-coded in post_to_srgb.frag. Because
        // the renderer now sets these every frame, change them here rather than in the shader.
        animated_floats<3> params{std::array<float, 3>{0.07f, 1.16f, 0.9f}};
    };

    inline film_grain_settings &get_film_grain_settings()
    {
        static film_grain_settings settings;
        return settings;
    }

    inline vignette_settings &get_vignette_settings()
    {
        static vignette_settings settings;
        return settings;
    }

    inline color_filter_settings &get_color_filter_settings()
    {
        static color_filter_settings settings;
        return settings;
    }

    // Call after post_to_srgb.use_program() and before drawing the final pass.
    inline void apply_post_fx(shaders::post_to_srgb_shader &shader)
    {
        double const now = effect_clock_now();

        // Film grain
        auto const grain = get_film_grain_settings().params.value(now);
        float const amount = std::clamp(grain[0], 0.0f, 1.0f);
        float const size = std::max(grain[1], 1.0f);
        float const rate = std::max(grain[2], 0.0f);

        // The shader picks new noise whenever the seed changes. The seed advances `rate` times
        // per second and wraps so it stays exactly representable as a float.
        float const seed =
            rate > 0.0f ? static_cast<float>(std::fmod(std::floor(now * rate), 1024.0)) : 0.0f;

        shader.set_film_grain(make_point(amount, size, seed, 0.0f));

        // Vignette
        auto const vig = get_vignette_settings().params.value(now);
        shader.set_vignette(make_point(std::clamp(vig[0], 0.0f, 1.0f),
                                       std::clamp(vig[1], 0.0f, 1.0f),
                                       std::clamp(vig[2], 0.0f, 1.0f),
                                       std::clamp(vig[3], 0.0f, 1.0f)));

        // Brightness / contrast / saturation
        auto const cf = get_color_filter_settings().params.value(now);
        shader.set_color_filter(std::clamp(cf[0], -1.0f, 1.0f),
                                std::clamp(cf[1], 0.0f, 4.0f),
                                std::clamp(cf[2], 0.0f, 4.0f));
    }
}
