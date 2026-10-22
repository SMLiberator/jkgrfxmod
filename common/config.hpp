#pragma once

#include "projected_decal.hpp"
#include <memory>
#include <optional>
#include <string>
#include <tuple>

namespace jkgm {
    struct antialiasing_config {
        std::string type;
        int samples = 2;
        float min_sample_factor = 0.0f;
    };

    class config {
    public:
        std::tuple<int, int> resolution = std::make_tuple(640, 480);
        bool fullscreen = false;
        bool correct_game_aspect_ratio = false;
        bool correct_menu_aspect_ratio = true;
        float hud_scale = 1.0f;
        float max_anisotropy = 2.0f;
        std::optional<antialiasing_config> antialiasing;
        bool enable_bloom = true;
        // Spatial blur (distance + peripheral) and temporal feedback trails.
        bool enable_blur = true;
        bool enable_trails = true;
        bool enable_fog = true;
        bool enable_post = true;
        float auto_exposure_sensitivity = 0.0f; // 0=disabled, 1=normal, 2=strong
        bool enable_reflections = true;
        bool enable_dynamic_light_specular = false; // opt-in per-Thing point-light GGX on GPU cached 3DOs
        bool enable_ssao = true;
        bool enable_parallax = true;
        bool enable_texture_filtering = true;
        bool retro_mode = false; // CMP #0 RGB8 post-quantization, no dithering
        bool enable_posterized_lighting = false;
        bool enable_vsync = false;
        bool profile_renderer = false; // opt-in CPU and async GPU frame timings
        bool profile_auto_exposure = false; // optional blocking 1px readback diagnostics
        bool optimize_hud_uploads = true; // upload only changed HUD rows (disable for A/B test)
        std::optional<projected_decal_config> projected_decal; // opt-in static depth projector
        bool use_model_cache = false; // cache 3DO meshes as persistent GPU geometry
        bool mesh_revision_tracking = false; // experimental JK.dll COG mutation tracking
        bool enable_spotlights = false; // COG MAT-based screen-space spotlights
        bool enable_sprite_decals = false; // opt-in type-3 SPR projection onto level geometry
        bool enable_mesh_collision = false; // experimental JKGM collide=2 Detour (off by default)
        bool use_vanilla_renderer = false; // native EXE bootstrap: skip JKGM hooks
        std::string data_path = "jkgm";
        std::optional<std::string> log_path;
        uint64_t vram_texture_preload_size = 0U;
    };

    std::unique_ptr<config> load_config_file();
}