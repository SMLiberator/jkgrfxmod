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
        // Master switches for optional effects. Blur gates upcoming frame/spatial blur effects.
        bool enable_blur = true;
        bool enable_fog = true;
        bool enable_post = true;
        bool enable_reflections = true;
        bool enable_ssao = true;
        // none, normals, depth, ssao, ssao_raw
        std::string debug_render_mode = "none";
        // Diagnostic: bypass the patched jk.exe native normals and derive normals
        // from projected triangle geometry, as the old renderer did.
        bool debug_force_face_normals = false;
        // Diagnostic: clear native-normal validity bits after each D3D execute pass.
        bool debug_consume_native_normals = false;
        bool enable_parallax = true;
        bool enable_texture_filtering = true;
        bool enable_posterized_lighting = false;
        bool enable_vsync = false;
        std::string command = "jk.exe";
        std::string data_path = "jkgm";
        std::optional<std::string> log_path;
        uint64_t vram_texture_preload_size = 0U;
    };

    std::unique_ptr<config> load_config_file();
}