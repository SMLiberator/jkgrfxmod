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
        bool experimental_radial_fog = false; // camera-to-fragment fog distance, not forward depth
        bool experimental_exponential_fog = false; // smooth radial attenuation with no hard clear radius
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
        bool enable_baked_overbright = true; // authored vertex/ambient/extra limit: false=1, true=2 (combined HDR is unbounded)
        bool gpu_pixel_lighting = false; // opt-in pixel dynamic lighting for all cached world/3DO geometry; false=JK Gouraud
        bool enable_normal_mapped_diffuse = false; // experimental: mapped normals for GPU world/3DO added sun, spot and pixel point light
        bool restore_cmp_lights = false; // opt-in indexed textured MAT fullbright pixels; GeoMode 3 always retains native HW fullbright
        bool enable_sector_crossing_lighting = false; // experimental 3DO sector light/CMP/tint
        bool experimental_sector_crossing_vertex = false; // A/B: vertex instead of fragment sector selection
        bool enable_ssao = true;
        bool enable_gbuffer_packing = false; // A/B compact color, emission, oct normals/light, and pigment
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
        bool experimental_packed_3do_vertices = false; // opt-in 52-byte compressed GPU 3DO vertices
        bool experimental_auto_3do_lod = false; // force native GeoSet 0; select automatically generated GPU mesh LODs
        float gpu_3do_auto_lod_lod1_radius_px = 90.0f; // enter generated LOD1 below this projected radius
        float gpu_3do_auto_lod_lod2_radius_px = 28.0f; // enter generated LOD2 below this projected radius
        int gpu_3do_auto_lod_debug_mode = 0; // 0=screen-size auto; 1=force original; 2=force LOD1; 3=force LOD2
        int gpu_3do_auto_lod_generation_budget = 1; // 0 = unlimited CPU generation attempts per frame
        int gpu_3do_auto_lod_min_object_triangles = 12; // total visible hierarchy triangles per Thing; 0=disabled
        bool experimental_3do_occlusion_culling = false; // async main camera GPU Thing submission
        bool experimental_3do_native_occlusion_culling = false; // early rdModel3_Draw skip using completed GPU Thing queries
        int gpu_3do_occlusion_proxy_mode = 0; // 0=box; 1=nearest sphere depth; 2=center sphere depth (self-occlusion experiment)
        int gpu_3do_occlusion_query_budget = 128; // 0=unlimited stress test; 1..256 budgeted queries/frame
        int gpu_3do_occlusion_debug_view = 0; // 0=off, 1=x-ray wireframe proxies, 2=x-ray translucent filled proxies
        int gpu_3do_occlusion_min_triangles = 0; // test coverage: query any sized Thing, 0..50000
        bool experimental_planar_thing_discovery = false; // read-only 120-frame native 3DO diagnostics
        bool enable_reflection_capture = true; // realtime shared/cube/hemi/planar; EnvMaps and COG capture independent
        bool enable_reflection_probes = false; // specialized cube/hemi/planar; false uses shared
        std::string debug_reflection_mode = "off"; // off, cube, hemi, shared; world-direction capture viewer
        int reflection_capture_resolution = 128; // cube face/short planar dimension; hemi doubles this
        float reflection_capture_update_interval = 0.0f; // all realtime capture types; 0=every frame
        int envmap_capture_face_resolution = 128; // on-demand CaptureEnvMap: 128x128 faces -> 512x256 equirect
        unsigned int reflection_probe_max_owners = 4U; // max independently cached hemi/cube owners
        float reflection_probe_max_distance = 64.0f; // JK units; dedicated hemi/cube only, 0=unlimited
        bool experimental_screen_space_reflections = false; // low-res cached-world and GPU 3DO SSR
        float ssr_resolution_scale = 0.125f; // 0.125..1.0 of internal screen
        int ssr_ray_steps = 32; // 4..96 depth search samples per half-res pixel
        float ssr_ray_thickness = 0.6f; // world-space depth tolerance
        float ssr_max_distance = 64.0f; // JK world units
        bool enable_gpu_sprite_instancing = true; // compatible SPR instancing, disable for diagnostics
        bool enable_gpu_3do_instancing = false; // opt-in exact opaque 3DO batches
        bool experimental_multithreading = false; // opt-in CPU-only 3DO visibility workers
        bool experimental_half_res_fluid_sprites = false; // opt-in half-size fluid SPR layer
        unsigned int fluid_sprite_layer_budget = 0U; // 0=unlimited; 1..255 surviving layers per half-size pixel
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
        // Sky polygons as sunlight apertures; experimental, default-off.
        // Requires sunlight shadows so the per-cascade projection exists.
        bool experimental_sun_sky_aperture = false;
        bool experimental_sun_caustics = false; // world/3DO Fluid transmission, never sprites
        // Phase 1 only: measure conservative sky-covered caster groups.
        // Does not yet prune any casters or change shadow texture contents.
        // Actual static WORLD caster pruning (independent of diagnostics).
        bool experimental_sun_sky_caster_pruning = false;
        double sun_shadow_update_interval = 0.0; // seconds between full CSM checks; 0=every frame
        bool enable_sprite_shadows = false; // opt-in sun shadow casting for cached SPRs; reception unaffected
        bool enable_sun_dynamic_shadows = false; // eight-Thing atlas
        bool sun_shadow_cull_back_faces = false; // 3DO sunlight: back-face culling is opt-in
        bool sun_shadow_cull_front_faces = true; // 3DO sunlight: front-face culling by default
        unsigned int sun_dynamic_shadow_resolution = 256U; // baseline 64..1024
        float sun_dynamic_shadow_pcf_radius = 0.0f; // 0=original 1 bilinear shadow lookup; >0 guarded 4-tap PCF (0..2)
        float sun_shadow_receiver_bias_scale = 1.0f; // A/B contact-gap tuning (0..2)
        float sun_shadow_caster_bias_scale = 1.0f; // A/B shadow polygon offset (0..2)
        unsigned int sun_shadow_resolution = 512U; // near and default far 24-bit depth map
        unsigned int sun_shadow_far_resolution = 0U; // 0 = inherit near resolution
        float sun_shadow_near_pcf_radius = 0.0f; // 0=single HW PCF; >0 four samples
        float sun_shadow_far_pcf_radius = 0.0f; // independent four-sample filter
        float experimental_sun_pcf_intensity_softness = 1.0f; // max multiplier above base near/far radii
        float experimental_sun_pcf_intensity_reference = 2.0f; // sun brightness where base radius is restored
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
