#pragma once

#include "base/filesystem.hpp"
#include "math/color.hpp"
#include "math/point.hpp"
#include <optional>

namespace jkgm {
    enum class material_alpha_mode { blend, mask };
    // Distinct from alpha testing. Additive/multiply materials always render
    // in the transparency pass, even when the original JK face was opaque.
    enum class material_blend_mode { normal, additive, multiply };
    // Screen mode initially falls back to the world-aligned EnvMap; an SSR pass will
    // consume this choice in the next stage.
    enum class material_reflection_mode { envmap, screen, fixed, matcap };

    enum class material_pulse_wave { sine, square, flicker, random };

    struct material_emissive_pulse {
        material_pulse_wave wave = material_pulse_wave::sine;
        float rate = 1.0f;  // seconds per cycle / new random target
        float min = 0.0f;
        float max = 1.0f;
        float phase = 0.0f; // fraction of a cycle (also shifts random intervals)
    };

    class material {
    public:
        std::optional<fs::path> albedo_map;
        color albedo_factor = color::fill(1.0f);

        // Keep the game's own texture as the albedo instead of replacing it with an albedo map.
        // Lets a material add emissive, displacement or reflection maps, or change its alpha
        // mode, without shipping a copy of the original image.
        bool use_original_albedo = false;

        std::optional<fs::path> emissive_map;
        color_rgb emissive_factor = color_rgb::zero();

        // Animation is optional and uses the renderer's independent clock.
        // UV speeds are texture repetitions per second (U,V).
        point<2, float> albedo_scroll = point<2, float>::zero();
        point<2, float> emissive_scroll = point<2, float>::zero();
        std::optional<material_emissive_pulse> emissive_pulse;

        std::optional<fs::path> displacement_map;
        float displacement_factor = 0.0f;

        // Reflections. The surface map holds three values per texel (linear colorspace):
        // R = reflectivity, G = roughness, B = metalness. The factors below scale the map
        // values, or stand on their own if there is no map.
        std::optional<fs::path> surface_map;
        float reflection_strength = 0.0f;
        float roughness = 0.5f;
        float metalness = 0.0f;

        // Equirectangular environment image this material reflects. A material that does not set
        // one gets its own pack's default environment image (if the pack has one).
        std::optional<fs::path> env_map;
        material_reflection_mode reflection_mode = material_reflection_mode::envmap;
        // nullopt follows the global enable_texture_filtering setting.
        std::optional<bool> texture_filtering;

        material_alpha_mode alpha_mode = material_alpha_mode::blend;
        material_blend_mode blend_mode = material_blend_mode::normal;
        // Restrict decal polygons to nearby opaque scene geometry.
        bool depth_clip = false;
        float alpha_cutoff = 0.5f;
    };
}