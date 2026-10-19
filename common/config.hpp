#pragma once

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
        bool enable_reflections = true;
        bool enable_ssao = true;
        bool enable_parallax = true;
        bool enable_texture_filtering = true;
        bool retro_mode = false; // CMP #0 RGB8 post-quantization, no dithering
        bool enable_posterized_lighting = false;
        bool enable_vsync = false;
        bool profile_renderer = false; // opt-in CPU frame timings
        bool use_model_cache = false; // cache 3DO meshes as persistent GPU geometry
        bool use_vanilla_renderer = false; // native EXE bootstrap: skip JKGM hooks
        std::string data_path = "jkgm";
        std::optional<std::string> log_path;
        uint64_t vram_texture_preload_size = 0U;
    };

    std::unique_ptr<config> load_config_file();
}