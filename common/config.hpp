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
        // Separate experimental optimizations, each default-off.
        bool experimental_fused_bloom_downsample = false;
        bool experimental_skip_bloom_clears = false;
        bool experimental_native_triangle_uploads = false;
        // Spatial blur (distance + peripheral) and temporal feedback trails.
        bool enable_blur = true;
        // Experimental half-resolution horizontal blur, default-off.
        bool experimental_spatial_blur_half_res = false; // legacy alias for mode 1
        int experimental_spatial_blur_mode = 0; // 0=original, 1=half-horizontal, 2=both half + reconstruct
        bool enable_trails = true;
        bool enable_fog = true;
        bool enable_post = true;
        bool enable_hdr = false; // strict master switch for tone mapping, auto exposure and HDR tuning
        float auto_exposure_sensitivity = 0.4f; // 0=fixed exposure, 0..2=adaptive
        float hdr_exposure_compensation = 0.0f; // constant EV offset, used only with HDR
        float hdr_shadow_softness = 0.3f; // 0=original crisp ACES toe, 1=softest
        float hdr_color_crispness = 0.6f; // 0=hue-preserving, 1=original per-channel ACES
        bool enable_reflections = true;
        int world_cache_fluid_debug_mode = 0; // 0=normal; 1-8=fluid diagnostics; 9=raw light; 10=sector CMP; 11=JK fade; 12=force white light
        // Opt-in direct-light GGX for previously unconfigured GPU 3DO/world MATs.
        bool enable_default_specular = false;
        float default_specular_strength = 0.25f;
        float default_specular_roughness = 0.85f;
        bool enable_dynamic_light_specular = false; // opt-in per-Thing point-light GGX on GPU cached 3DOs
        bool gpu_pixel_lighting = false; // opt-in pixel dynamic lighting for all cached world/3DO geometry; false=JK Gouraud
        bool enable_normal_mapped_diffuse = false; // experimental: mapped normals for GPU world/3DO added sun, spot and pixel point light
        bool restore_cmp_lights = false; // opt-in indexed textured MAT fullbright pixels; GeoMode 3 always retains native HW fullbright
        bool enable_sector_crossing_lighting = false; // experimental per-fragment 3DO sector light/CMP/tint
        bool enable_ssao = true;
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
        bool use_gpu_cache = true; // unified GPU 3DO, sprite, and SithWorld rendering
        bool enable_gpu_sprite_instancing = true; // compatible SPR instancing, disable for diagnostics
        bool enable_saber_trails = true; // GPU-cached saber fan during marker 4/5 collision windows
        float saber_trail_delay = 0.06f; // seconds of delayed blade direction (0.005..0.25)
        bool enable_gpu_camera_hud = true; // Camera-type 3DOs: false forces native diagnostic (HUD then receives postFX)
        bool camera_hud_depth_sort = true; // diagnostic: false preserves authored draw order for translucent Camera HUDs
        bool enable_native_fallback = true; // false: suppress mixed native world batches for cache gap diagnostics
        bool enable_gpu_mat_warmup = true; // guarded deferred MAT uploader, disable for diagnostics
        bool enable_spotlights = false; // COG MAT-based screen-space spotlights
        bool enable_spotlight_shadows = false; // experimental, one static-world shadow map
        unsigned int spotlight_shadow_resolution = 256U; // depth-map width and height (64..1024)
        float spotlight_shadow_angle_bias_scale = 0.0f; // opt-in: 0=legacy bias, 1=adaptive +0.035 JKU at grazing (0..2)
        bool enable_sun = true; // diffuse sunlight from SetSunInfo; independent of shadow rendering
        bool enable_sun_shadows = false;
        double sun_shadow_update_interval = 0.0; // seconds between full CSM checks; 0=every frame
        bool enable_sprite_shadows = false; // opt-in sun shadow casting for cached SPRs; reception unaffected
        bool enable_sun_dynamic_shadows = false; // eight-Thing atlas
        bool sun_shadow_cull_back_faces = false; // 3DO sunlight: back-face culling is opt-in
        bool sun_shadow_cull_front_faces = true; // 3DO sunlight: front-face culling by default
        unsigned int sun_dynamic_shadow_resolution = 256U; // baseline 64..1024
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
