#pragma once

#include "base/filesystem.hpp"
#include "math/color.hpp"
#include <optional>

namespace jkgm {
    enum class material_alpha_mode { blend, mask };

    class material {
    public:
        std::optional<fs::path> albedo_map;
        color albedo_factor = color::fill(1.0f);

        std::optional<fs::path> emissive_map;
        color_rgb emissive_factor = color_rgb::zero();

        std::optional<fs::path> displacement_map;
        float displacement_factor = 0.0f;

        // Reflections. The surface map holds three values per texel (linear colorspace):
        // R = reflectivity, G = roughness, B = metalness. The factors below scale the map
        // values, or stand on their own if there is no map.
        std::optional<fs::path> surface_map;
        float reflection_strength = 0.0f;
        float roughness = 0.5f;
        float metalness = 0.0f;

        // Equirectangular environment image this material reflects. If unset, the material
        // pack's default environment (if any) is used.
        std::optional<fs::path> env_map;

        material_alpha_mode alpha_mode = material_alpha_mode::blend;
        float alpha_cutoff = 0.5f;
    };
}