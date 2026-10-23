#pragma once

#include "animated.hpp"
#include "shaders.hpp"
#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>

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
        // strength: 0.0 - 2.0
        //   0 = off, 1 = the classic vignette
        //   1 - 2 closes the vignette in like an iris, reaching a solid screen of the vignette
        //   color at 2 (a fade to black, or to red, etc.)
        //
        // Vanilla JK has no vignette. Levels and mods explicitly enable it
        // with SetVignette; ResetVignette returns to this neutral state.
        animated_floats<4> params{std::array<float, 4>{0.0f, 0.0f, 0.0f, 0.0f}};
    };

    struct color_filter_settings {
        // brightness: -1.0 - 1.0, contrast: 0.0 - 4.0, saturation: 0.0 - 4.0
        //
        // Identity grade by default; mod/level COGs choose the actual look.
        // ResetColorFilter() also returns to this neutral state.
        animated_floats<3> params{std::array<float, 3>{0.0f, 1.0f, 1.0f}};
    };

    // Recursive temporal accumulation. 0 = no trails, 1 = hold the previous
    // output indefinitely. The history must be invalidated on reset or scene changes.
    // Screen-space depth blur: distances are JK's world/view units.
    // x=start, y=end, z=strength (0..1). No blur at zero strength.
    struct distance_blur_settings {
        animated_floats<3> params{std::array<float, 3>{64.0f, 256.0f, 0.0f}};
    };

    // Radius from screen center normalized to 0=center, 1=corner.
    // x=inner, y=outer, z=strength (0..1).
    struct peripheral_blur_settings {
        animated_floats<3> params{std::array<float, 3>{0.55f, 1.0f, 0.0f}};
    };

    inline distance_blur_settings &get_distance_blur_settings()
    {
        static distance_blur_settings settings;
        return settings;
    }

    inline peripheral_blur_settings &get_peripheral_blur_settings()
    {
        static peripheral_blur_settings settings;
        return settings;
    }

    struct frame_blend_settings {
        animated_floats<1> strength{std::array<float, 1>{0.0f}};
        uint64_t reset_generation = 0;

        void reset()
        {
            strength.set({0.0f});
            ++reset_generation;
        }
    };

    inline frame_blend_settings &get_frame_blend_settings()
    {
        static frame_blend_settings settings;
        return settings;
    }

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
    inline void apply_post_fx(shaders::post_to_srgb_shader &shader, bool enable_post)
    {
        if(!enable_post) {
            // Preserve mandatory linear->sRGB presentation but bypass optional effects.
            shader.set_film_grain(make_point(0.0f, 1.0f, 0.0f, 0.0f));
            shader.set_vignette(make_point(0.0f, 0.0f, 0.0f, 0.0f));
            shader.set_color_filter(0.0f, 1.0f, 1.0f);
            return;
        }

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
                                       std::clamp(vig[3], 0.0f, 2.0f)));

        // Brightness / contrast / saturation
        auto const cf = get_color_filter_settings().params.value(now);
        shader.set_color_filter(std::clamp(cf[0], -1.0f, 1.0f),
                                std::clamp(cf[1], 0.0f, 4.0f),
                                std::clamp(cf[2], 0.0f, 4.0f));
    }
}
