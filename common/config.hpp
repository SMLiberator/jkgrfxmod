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
        bool enable_hdr = false; // strict master switch for tone mapping, auto exposure and HDR tuning
        float auto_exposure_sensitivity = 0.4f; // 0=fixed exposure, 0..2=adaptive
        float hdr_exposure_compensation = 0.0f; // constant EV offset, used only with HDR
        float hdr_shadow_softness = 0.3f; // 0=original crisp ACES toe, 1=softest
        float hdr_color_crispness = 0.6f; // 0=hue-preserving, 1=original per-channel ACES
        bool enable_reflections = true;
        bool enable_dynamic_light_specular = false; // opt-in per-Thing point-light GGX on GPU cached 3DOs
        bool enable_ssao = true;
        bool fill_tjunction_gaps = false; // opt-in G-buffer gap repair
        unsigned int tjunction_gap_fill_size = 2U; // proven useful screen-pixel radius, 0..8; disabled unless fill_tjunction_gaps
        bool debug_void_pixels = false; // show raw unresolved empty-depth pixels as neon checkerboard
        bool retain_hom_pixels = true; // vanilla JK HOM default; false = Clear Display
        bool enable_parallax = true;
        bool enable_texture_filtering = true;
        bool retro_mode = false; // CMP #0 RGB8 post-quantization, no dithering
        bool enable_posterized_lighting = false;
        bool enable_vsync = false;
        bool profile_renderer = false; // opt-in CPU and async GPU frame timings
        bool profile_renderer_verbose = false; // opt-in detailed renderer diagnostics
        bool profile_audio = false; // read-only A3D/DirectSound DLL diagnostics
        bool profile_auto_exposure = false; // optional blocking 1px readback diagnostics
        bool optimize_hud_uploads = true; // upload only changed HUD rows (disable for A/B test)
        bool use_model_cache = false; // cache 3DO meshes as persistent GPU geometry
        bool use_sprite_cache = false; // experimental shared GPU quad caching for SPRs
        bool mesh_revision_tracking = false; // experimental JK.dll COG mutation tracking
        bool enable_spotlights = false; // COG MAT-based screen-space spotlights
        bool enable_spotlight_shadows = false; // experimental, one static-world shadow map
        unsigned int spotlight_shadow_resolution = 256U; // depth-map width and height (64..1024)
        bool enable_sun = true; // diffuse sunlight from SetSunInfo; independent of shadow rendering
        bool enable_sun_shadows = false;
        bool enable_sun_static_cache = true; // world-only depth layers, independently cached per cascade // directional sunlight + world shadow map
        bool sun_shadow_stable_projection_cache = false; // opt-in reuse while camera frustum stays inside map
        bool sun_shadow_instance_batches = false; // opt-in GL instancing for repeated 3DO sunlight casters
        bool enable_sun_dynamic_shadows = false; // eight-Thing atlas
        bool sun_shadow_cull_back_faces = false; // 3DO sunlight: back-face culling is opt-in
        bool sun_shadow_cull_front_faces = true; // 3DO sunlight: front-face culling by default
        unsigned int sun_dynamic_shadow_resolution = 256U; // baseline 64..1024
        bool sun_dynamic_shadow_adaptive_resolution = false; // opt-in per-Thing pixel density
        float sun_shadow_receiver_bias_scale = 1.0f; // A/B contact-gap tuning (0..2)
        float sun_shadow_caster_bias_scale = 1.0f; // A/B shadow polygon offset (0..2)
        unsigned int sun_shadow_resolution = 512U; // per-cascade 24-bit depth map (64..4096)
        float sun_shadow_near_distance = 16.0f; // camera-local near cascade, 2..128
        float sun_shadow_far_distance = 128.0f; // outer cascade coverage, 4..512
        float sun_max_distance = 0.0f; // 0=unlimited; else sun light+shadows fade out and stop at this camera distance
        bool enable_decals = true; // type-3 SPR projection; false hides decal SPRs, not native billboards
        bool enable_mesh_collision = false; // experimental JKGM collide=2 Detour (off by default)
        bool use_vanilla_renderer = false; // native EXE bootstrap: skip JKGM hooks
        std::string data_path = "jkgm";
        std::optional<std::string> log_path;
        uint64_t vram_texture_preload_size = 0U;
    };

    std::unique_ptr<config> load_config_file();
}
