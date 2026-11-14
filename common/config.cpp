#include "config.hpp"
#include "base/file_stream.hpp"
#include "base/log.hpp"
#include "base/memory_block.hpp"
#include "error_reporter.hpp"
#include "json_incl.hpp"
#include <cmath>
#include <stdexcept>

std::unique_ptr<jkgm::config> jkgm::load_config_file()
{
    diagnostic_context dc("jkgm.json");
    auto rv = std::make_unique<config>();

    try {
        auto fs = jkgm::make_file_input_stream("jkgm.json");

        memory_block mb;
        memory_output_block mob(&mb);
        fs->copy_to(&mob);

        auto j = json::json::parse(mb.str());
        if(j.contains("resolution")) {
            j.at("resolution").get_to(rv->resolution);
        }

        if(j.contains("fullscreen")) {
            j.at("fullscreen").get_to(rv->fullscreen);
        }

        if(j.contains("correct_game_aspect_ratio")) {
            j.at("correct_game_aspect_ratio").get_to(rv->correct_game_aspect_ratio);
        }

        if(j.contains("correct_menu_aspect_ratio")) {
            j.at("correct_menu_aspect_ratio").get_to(rv->correct_menu_aspect_ratio);
        }

        if(j.contains("hud_scale")) {
            j.at("hud_scale").get_to(rv->hud_scale);
        }

        if(j.contains("max_anisotropy")) {
            j.at("max_anisotropy").get_to(rv->max_anisotropy);
        }

        if(j.contains("antialiasing")) {
            auto const &em = j["antialiasing"];
            if(!em.is_null()) {
                antialiasing_config aa;
                aa.type = em.at("type").get_to(aa.type);

                if(em.contains("samples")) {
                    em.at("samples").get_to(aa.samples);
                }

                if(em.contains("min_sample_factor")) {
                    em.at("min_sample_factor").get_to(aa.min_sample_factor);
                }

                rv->antialiasing = aa;
            }
        }

        if(j.contains("enable_bloom")) {
            j.at("enable_bloom").get_to(rv->enable_bloom);
        }
        if(j.contains("experimental_fused_bloom_downsample")) {
            j.at("experimental_fused_bloom_downsample").get_to(
                rv->experimental_fused_bloom_downsample);
        }
        if(j.contains("experimental_skip_bloom_clears")) {
            j.at("experimental_skip_bloom_clears").get_to(
                rv->experimental_skip_bloom_clears);
        }
        if(j.contains("experimental_native_triangle_uploads")) {
            j.at("experimental_native_triangle_uploads").get_to(
                rv->experimental_native_triangle_uploads);
        }

        if(j.contains("enable_blur")) {
            j.at("enable_blur").get_to(rv->enable_blur);
        }
        if(j.contains("experimental_spatial_blur_half_res")) {
            j.at("experimental_spatial_blur_half_res").get_to(
                rv->experimental_spatial_blur_half_res);
        }
        // Canonical mode takes precedence over both historical keys.
        // Retain the two experimental JSON names for existing user configs.
        if(j.contains("spatial_blur_mode") ||
           j.contains("experimental_spatial_blur_mode")) {
            char const *key=j.contains("spatial_blur_mode")
                ?"spatial_blur_mode":"experimental_spatial_blur_mode";
            auto const &mode=j.at(key);
            if(!mode.is_number_integer() || mode.get<int>()<0 ||
               mode.get<int>()>2)
                throw std::runtime_error(std::string(key)+
                                         " must be 0, 1 or 2");
            rv->spatial_blur_mode=mode.get<int>();
        }
        else if(rv->experimental_spatial_blur_half_res)
            rv->spatial_blur_mode=1;
        if(j.contains("enable_trails")) {
            j.at("enable_trails").get_to(rv->enable_trails);
        }

        if(j.contains("enable_fog")) {
            j.at("enable_fog").get_to(rv->enable_fog);
        }

        if(j.contains("enable_post")) {
            j.at("enable_post").get_to(rv->enable_post);
        }

        if(j.contains("enable_hdr")) {
            j.at("enable_hdr").get_to(rv->enable_hdr);
        }

        if(rv->enable_hdr && j.contains("hdr_exposure_compensation")) {
            auto const &v=j.at("hdr_exposure_compensation");
            if(!v.is_number() || !std::isfinite(static_cast<double>(v))) {
                throw std::runtime_error("hdr_exposure_compensation must be a finite number");
            }
            rv->hdr_exposure_compensation=static_cast<float>(v);
            if(!std::isfinite(rv->hdr_exposure_compensation) ||
               rv->hdr_exposure_compensation<-3.0f ||
               rv->hdr_exposure_compensation>3.0f) {
                throw std::runtime_error("hdr_exposure_compensation must be between -3 and 3");
            }
        }

        if(rv->enable_hdr && j.contains("hdr_shadow_softness")) {
            auto const &v=j.at("hdr_shadow_softness");
            if(!v.is_number() || !std::isfinite(static_cast<double>(v)))
                throw std::runtime_error("hdr_shadow_softness must be finite");
            rv->hdr_shadow_softness=static_cast<float>(v);
            if(!std::isfinite(rv->hdr_shadow_softness) ||
               rv->hdr_shadow_softness<0.0f ||
               rv->hdr_shadow_softness>1.0f)
                throw std::runtime_error("hdr_shadow_softness must be between 0 and 1");
        }

        if(rv->enable_hdr && j.contains("hdr_color_crispness")) {
            auto const &v=j.at("hdr_color_crispness");
            if(!v.is_number() || !std::isfinite(static_cast<double>(v)))
                throw std::runtime_error("hdr_color_crispness must be finite");
            rv->hdr_color_crispness=static_cast<float>(v);
            if(!std::isfinite(rv->hdr_color_crispness) ||
               rv->hdr_color_crispness<0.0f || rv->hdr_color_crispness>1.0f)
                throw std::runtime_error("hdr_color_crispness must be between 0 and 1");
        }

        if(rv->enable_hdr && j.contains("auto_exposure_sensitivity")) {
            auto const &v=j.at("auto_exposure_sensitivity");
            if(!v.is_number() || !std::isfinite(static_cast<double>(v))) {
                throw std::runtime_error("auto_exposure_sensitivity must be a finite number");
            }
            rv->auto_exposure_sensitivity=static_cast<float>(v);
            if(!std::isfinite(rv->auto_exposure_sensitivity) ||
               rv->auto_exposure_sensitivity<0.0f ||
               rv->auto_exposure_sensitivity>2.0f) {
                throw std::runtime_error("auto_exposure_sensitivity must be between 0 and 2");
            }
        }

        if(j.contains("enable_reflections")) {
            j.at("enable_reflections").get_to(rv->enable_reflections);
        }
        if(j.contains("world_cache_fluid_debug_mode")) {
            auto const &mode=j.at("world_cache_fluid_debug_mode");
            if(!mode.is_number_integer())
                throw std::runtime_error(
                    "world_cache_fluid_debug_mode must be an integer from 0 to 12");
            int const value=mode.get<int>();
            if(value<0 || value>12)
                throw std::runtime_error(
                    "world_cache_fluid_debug_mode must be an integer from 0 to 12");
            rv->world_cache_fluid_debug_mode=value;
        }
        if(j.contains("enable_default_specular")) {
            j.at("enable_default_specular").get_to(rv->enable_default_specular);
        }
        // Keep experimental tuning valid even when the feature is disabled:
        // users can freely A/B the toggle without hiding invalid settings.
        auto read_specular_scalar=[&](char const *key,float &target) {
            if(!j.contains(key))return;
            auto const &v=j.at(key);
            if(!v.is_number()) {
                throw std::runtime_error(std::string(key)+
                    " must be a finite number between 0 and 1");
            }
            double const number=v.get<double>();
            if(!std::isfinite(number) || number<0.0 || number>1.0) {
                throw std::runtime_error(std::string(key)+
                    " must be a finite number between 0 and 1");
            }
            target=static_cast<float>(number);
        };
        read_specular_scalar("default_specular_strength",
                             rv->default_specular_strength);
        read_specular_scalar("default_specular_roughness",
                             rv->default_specular_roughness);
        if(j.contains("experimental_specular_normal_mode")) {
            auto const &v=j.at("experimental_specular_normal_mode");
            if(!v.is_number_integer() || v.get<int>()<0 || v.get<int>()>2)
                throw std::runtime_error(
                    "experimental_specular_normal_mode must be 0, 1 or 2");
            rv->experimental_specular_normal_mode=v.get<int>();
        }
        if(j.contains("enable_dynamic_light_specular")) {
            j.at("enable_dynamic_light_specular").get_to(
                rv->enable_dynamic_light_specular);
        }
        // One canonical switch for baked vertex, sector ambient, and extra
        // light. Older experimental names are accepted only as aliases.
        if(j.contains("enable_baked_overbright")) {
            j.at("enable_baked_overbright").get_to(rv->enable_baked_overbright);
        } else if(j.contains("overbright_intensities")) {
            j.at("overbright_intensities").get_to(rv->enable_baked_overbright);
        } else if(j.contains("enable_baked_vertex_overbrightness")) {
            j.at("enable_baked_vertex_overbrightness").get_to(
                rv->enable_baked_overbright);
        }
        for(auto const *legacy:{
                "overbright_intensities",
                "enable_baked_vertex_overbrightness"}) {
            if(j.contains(legacy))
                LOG_WARNING("Deprecated jkgm.json option ignored or aliased: ",
                    legacy," (use enable_baked_overbright)");
        }
        if(j.contains("gpu_pixel_lighting")) {
            j.at("gpu_pixel_lighting").get_to(rv->gpu_pixel_lighting);
        }
        if(j.contains("enable_normal_mapped_diffuse")) {
            j.at("enable_normal_mapped_diffuse").get_to(
                rv->enable_normal_mapped_diffuse);
        }
        if(j.contains("restore_cmp_lights")) {
            j.at("restore_cmp_lights").get_to(rv->restore_cmp_lights);
        }
        if(j.contains("enable_sector_crossing_lighting")) {
            j.at("enable_sector_crossing_lighting").get_to(
                rv->enable_sector_crossing_lighting);
        }
        if(j.contains("experimental_sector_crossing_vertex")) {
            j.at("experimental_sector_crossing_vertex").get_to(
                rv->experimental_sector_crossing_vertex);
        }
        if(j.contains("experimental_sector_crossing_owner_cull")) {
            j.at("experimental_sector_crossing_owner_cull").get_to(
                rv->experimental_sector_crossing_owner_cull);
        }
        if(j.contains("experimental_continuous_ambient_gpu")) {
            j.at("experimental_continuous_ambient_gpu").get_to(
                rv->experimental_continuous_ambient_gpu);
        }
        if(j.contains("enable_ssao")) {
            j.at("enable_ssao").get_to(rv->enable_ssao);
        }
        if(j.contains("experimental_half_res_ssao")) {
            j.at("experimental_half_res_ssao").get_to(
                rv->experimental_half_res_ssao);
        }
        if(j.contains("experimental_half_res_sun_visibility")) {
            j.at("experimental_half_res_sun_visibility").get_to(
                rv->experimental_half_res_sun_visibility);
        }

        if(j.contains("enable_parallax")) {
            j.at("enable_parallax").get_to(rv->enable_parallax);
        }

        if(j.contains("enable_texture_filtering")) {
            j.at("enable_texture_filtering").get_to(rv->enable_texture_filtering);
        }

        if(j.contains("retro_mode")) {
            j.at("retro_mode").get_to(rv->retro_mode);
        }
        if(j.contains("enable_posterized_lighting")) {
            j.at("enable_posterized_lighting").get_to(rv->enable_posterized_lighting);
        }

        if(j.contains("enable_vsync")) {
            j.at("enable_vsync").get_to(rv->enable_vsync);
        }
        if(j.contains("profile_renderer")) {
            j.at("profile_renderer").get_to(rv->profile_renderer);
        }
        if(j.contains("profile_renderer_verbose")) {
            j.at("profile_renderer_verbose").get_to(rv->profile_renderer_verbose);
        }
        if(j.contains("profile_audio")) {
            j.at("profile_audio").get_to(rv->profile_audio);
        }
        if(j.contains("use_gpu_cache")) {
            j.at("use_gpu_cache").get_to(rv->use_gpu_cache);
        }
        if(j.contains("experimental_3do_occlusion_culling")) {
            j.at("experimental_3do_occlusion_culling").get_to(
                rv->experimental_3do_occlusion_culling);
        }
        if(j.contains("experimental_3do_native_occlusion_culling")) {
            j.at("experimental_3do_native_occlusion_culling").get_to(
                rv->experimental_3do_native_occlusion_culling);
        }
        if(j.contains("gpu_3do_occlusion_proxy_mode")) {
            auto const &v=j.at("gpu_3do_occlusion_proxy_mode");
            if(!v.is_number_integer())
                throw std::runtime_error(
                    "gpu_3do_occlusion_proxy_mode must be integer 0..2");
            int const n=v.get<int>();
            if(n<0 || n>2)
                throw std::runtime_error(
                    "gpu_3do_occlusion_proxy_mode must be between 0 and 2");
            rv->gpu_3do_occlusion_proxy_mode=n;
        }
        if(j.contains("gpu_3do_occlusion_query_budget")) {
            auto const &v=j.at("gpu_3do_occlusion_query_budget");
            if(!v.is_number_integer())
                throw std::runtime_error(
                    "gpu_3do_occlusion_query_budget must be integer 0..256");
            int const n=v.get<int>();
            if(n<0 || n>256)
                throw std::runtime_error(
                    "gpu_3do_occlusion_query_budget must be between 0 and 256");
            rv->gpu_3do_occlusion_query_budget=n;
        }
        if(j.contains("gpu_3do_occlusion_debug_view")) {
            auto const &v=j.at("gpu_3do_occlusion_debug_view");
            if(!v.is_number_integer())
                throw std::runtime_error(
                    "gpu_3do_occlusion_debug_view must be integer 0..2");
            int const n=v.get<int>();
            if(n<0 || n>2)
                throw std::runtime_error(
                    "gpu_3do_occlusion_debug_view must be between 0 and 2");
            rv->gpu_3do_occlusion_debug_view=n;
        }
        if(j.contains("gpu_3do_occlusion_min_triangles")) {
            auto const &v=j.at("gpu_3do_occlusion_min_triangles");
            if(!v.is_number_integer())
                throw std::runtime_error(
                    "gpu_3do_occlusion_min_triangles must be integer 0..50000");
            int const n=v.get<int>();
            if(n<0 || n>50000)
                throw std::runtime_error(
                    "gpu_3do_occlusion_min_triangles must be between 0 and 50000");
            rv->gpu_3do_occlusion_min_triangles=n;
        }
        if(j.contains("experimental_planar_thing_discovery")) {
            j.at("experimental_planar_thing_discovery").get_to(
                rv->experimental_planar_thing_discovery);
        }
        if(j.contains("experimental_planar_reflection_capture")) {
            j.at("experimental_planar_reflection_capture").get_to(
                rv->experimental_planar_reflection_capture);
        }
        if(j.contains("planar_reflection_resolution_scale")) {
            auto const &s=j.at("planar_reflection_resolution_scale");
            if(!s.is_number())throw std::runtime_error(
                "planar_reflection_resolution_scale must be a finite number between 0.125 and 1.0");
            float const v=s.get<float>();
            if(!std::isfinite(v) || v<0.125f || v>1.0f)
                throw std::runtime_error(
                    "planar_reflection_resolution_scale must be between 0.25 and 1.0");
            rv->planar_reflection_resolution_scale=v;
        }
        if(j.contains("enable_gpu_sprite_instancing")) {
            j.at("enable_gpu_sprite_instancing").get_to(
                rv->enable_gpu_sprite_instancing);
        }
        if(j.contains("experimental_half_res_fluid_sprites")) {
            j.at("experimental_half_res_fluid_sprites").get_to(
                rv->experimental_half_res_fluid_sprites);
        }
        if(j.contains("fluid_sprite_layer_budget")) {
            auto const &value=j.at("fluid_sprite_layer_budget");
            if(!value.is_number_integer()) {
                throw std::runtime_error(
                    "fluid_sprite_layer_budget must be an integer from 0 to 255");
            }
            int const budget=value.get<int>();
            if(budget<0 || budget>255) {
                throw std::runtime_error(
                    "fluid_sprite_layer_budget must be an integer from 0 to 255");
            }
            rv->fluid_sprite_layer_budget=static_cast<unsigned int>(budget);
        }
        if(j.contains("enable_saber_trails")) {
            j.at("enable_saber_trails").get_to(rv->enable_saber_trails);
        }
        if(j.contains("saber_trail_delay")) {
            auto const &value=j.at("saber_trail_delay");
            if(!value.is_number())
                throw std::runtime_error("saber_trail_delay must be a number");
            double const seconds=value.get<double>();
            if(!std::isfinite(seconds) || seconds<0.005 || seconds>0.25)
                throw std::runtime_error("saber_trail_delay must be between 0.005 and 0.25 seconds");
            rv->saber_trail_delay=static_cast<float>(seconds);
        }
        if(j.contains("enable_gpu_camera_hud")) {
            j.at("enable_gpu_camera_hud").get_to(rv->enable_gpu_camera_hud);
        }
        if(j.contains("camera_hud_depth_sort")) {
            j.at("camera_hud_depth_sort").get_to(rv->camera_hud_depth_sort);
        }
        // Retired toggles no longer override the single cache setting.
        // Warn explicitly so old configs don't silently switch modes.
        for(auto const *legacy:{
                "use_model_cache","use_sprite_cache","world_cache_preview",
                "mesh_revision_tracking","optimize_hud_uploads",
                "sun_dynamic_shadow_adaptive_resolution",
                "enable_sun_static_cache","sun_shadow_stable_projection_cache",
                "sun_shadow_instance_batches"}) {
            if(j.contains(legacy))
                LOG_WARNING("Deprecated jkgm.json option ignored: ",legacy,
                            " (GPU caching and established mesh/HUD/sun-shadow "
                            "optimizations now use their standard behavior)");
        }
        if(j.contains("enable_native_fallback")) {
            j.at("enable_native_fallback").get_to(rv->enable_native_fallback);
        }
        if(j.contains("enable_gpu_mat_warmup")) {
            j.at("enable_gpu_mat_warmup").get_to(rv->enable_gpu_mat_warmup);
        }
        if(j.contains("gpu_mat_preload") &&
           j.at("gpu_mat_preload").is_boolean() &&
           j.at("gpu_mat_preload").get<bool>()) {
            LOG_WARNING("gpu_mat_preload was removed after a native MAT uploader "
                        "crash during level loading; falling back to safe native warm-up");
        }
        if(j.contains("enable_spotlights")) {
            j.at("enable_spotlights").get_to(rv->enable_spotlights);
        }
        if(j.contains("enable_spotlight_shadows")) {
            j.at("enable_spotlight_shadows").get_to(rv->enable_spotlight_shadows);
        }
        if(j.contains("spotlight_shadow_resolution")) {
            auto const &v = j.at("spotlight_shadow_resolution");
            if(!v.is_number_integer()) {
                throw std::runtime_error(
                    "spotlight_shadow_resolution must be an integer between 64 and 1024");
            }
            auto const pixels = v.get<long long>();
            if(pixels < 64 || pixels > 1024) {
                throw std::runtime_error(
                    "spotlight_shadow_resolution must be between 64 and 1024");
            }
            rv->spotlight_shadow_resolution = static_cast<unsigned int>(pixels);
        }
        if(j.contains("spotlight_shadow_angle_bias_scale")) {
            auto const &v=j.at("spotlight_shadow_angle_bias_scale");
            if(!v.is_number())
                throw std::runtime_error(
                    "spotlight_shadow_angle_bias_scale must be numeric (0..2)");
            double const scale=v.get<double>();
            if(!std::isfinite(scale) || scale<0.0 || scale>2.0)
                throw std::runtime_error(
                    "spotlight_shadow_angle_bias_scale must be between 0 and 2");
            rv->spotlight_shadow_angle_bias_scale=static_cast<float>(scale);
        }
        if(j.contains("retain_hom_pixels")) {
            j.at("retain_hom_pixels").get_to(rv->retain_hom_pixels);
        }
        if(j.contains("enable_sun")) {
            j.at("enable_sun").get_to(rv->enable_sun);
        }
        if(j.contains("enable_sun_shadows")) {
            j.at("enable_sun_shadows").get_to(rv->enable_sun_shadows);
        }
        if(j.contains("experimental_sun_sky_aperture")) {
            j.at("experimental_sun_sky_aperture").get_to(
                rv->experimental_sun_sky_aperture);
        }
        if(j.contains("experimental_sun_sky_caster_pruning")) {
            j.at("experimental_sun_sky_caster_pruning").get_to(
                rv->experimental_sun_sky_caster_pruning);
        }
        if(j.contains("sun_shadow_update_interval")) {
            auto const &v=j.at("sun_shadow_update_interval");
            if(!v.is_number())
                throw std::runtime_error(
                    "sun_shadow_update_interval must be numeric (0..2 seconds)");
            double const seconds=v.get<double>();
            if(!std::isfinite(seconds) || seconds<0.0 || seconds>2.0)
                throw std::runtime_error(
                    "sun_shadow_update_interval must be between 0 and 2 seconds");
            rv->sun_shadow_update_interval=seconds;
        }
        if(j.contains("enable_sprite_shadows")) {
            j.at("enable_sprite_shadows").get_to(rv->enable_sprite_shadows);
        }
        if(j.contains("enable_sun_dynamic_shadows")) {
            j.at("enable_sun_dynamic_shadows").get_to(rv->enable_sun_dynamic_shadows);
        }
        bool const has_sun_cull_back=j.contains("sun_shadow_cull_back_faces");
        bool const has_sun_cull_front=j.contains("sun_shadow_cull_front_faces");
        if(has_sun_cull_back) {
            j.at("sun_shadow_cull_back_faces").get_to(rv->sun_shadow_cull_back_faces);
        }
        if(has_sun_cull_front) {
            j.at("sun_shadow_cull_front_faces").get_to(rv->sun_shadow_cull_front_faces);
        }
        // Back-culling-only legacy configurations must continue to work now
        // that the front-face default is enabled. Explicitly setting both
        // switches still takes priority (false/false = two-sided).
        if(has_sun_cull_back && !has_sun_cull_front &&
           rv->sun_shadow_cull_back_faces)
            rv->sun_shadow_cull_front_faces=false;
        if(rv->sun_shadow_cull_back_faces && rv->sun_shadow_cull_front_faces)
            throw std::runtime_error("sun_shadow_cull_back_faces and sun_shadow_cull_front_faces cannot both be true");
        if(j.contains("sun_dynamic_shadow_resolution")) {
            auto const &v=j.at("sun_dynamic_shadow_resolution");
            if(!v.is_number_integer())
                throw std::runtime_error(
                    "sun_dynamic_shadow_resolution must be an integer between 64 and 1024");
            auto const pixels=v.get<long long>();
            if(pixels<64 || pixels>1024)
                throw std::runtime_error(
                    "sun_dynamic_shadow_resolution must be between 64 and 1024");
            rv->sun_dynamic_shadow_resolution=static_cast<unsigned int>(pixels);
        }
        if(j.contains("sun_dynamic_shadow_pcf_radius")) {
            auto const &v=j.at("sun_dynamic_shadow_pcf_radius");
            if(!v.is_number())
                throw std::runtime_error(
                    "sun_dynamic_shadow_pcf_radius must be numeric (0..2)");
            double const radius=v.get<double>();
            if(!std::isfinite(radius) || radius<0.0 || radius>2.0)
                throw std::runtime_error(
                    "sun_dynamic_shadow_pcf_radius must be between 0 and 2");
            rv->sun_dynamic_shadow_pcf_radius=static_cast<float>(radius);
        }
        if(j.contains("sun_shadow_receiver_bias_scale")) {
            auto const &value=j.at("sun_shadow_receiver_bias_scale");
            if(!value.is_number())
                throw std::runtime_error("sun_shadow_receiver_bias_scale must be numeric (0..2)");
            double const factor=value.get<double>();
            if(!std::isfinite(factor) || factor<0.0 || factor>2.0)
                throw std::runtime_error("sun_shadow_receiver_bias_scale must be between 0 and 2");
            rv->sun_shadow_receiver_bias_scale=static_cast<float>(factor);
        }
        if(j.contains("sun_shadow_caster_bias_scale")) {
            auto const &value=j.at("sun_shadow_caster_bias_scale");
            if(!value.is_number())
                throw std::runtime_error("sun_shadow_caster_bias_scale must be numeric (0..2)");
            double const factor=value.get<double>();
            if(!std::isfinite(factor) || factor<0.0 || factor>2.0)
                throw std::runtime_error("sun_shadow_caster_bias_scale must be between 0 and 2");
            rv->sun_shadow_caster_bias_scale=static_cast<float>(factor);
        }
        if(j.contains("sun_shadow_near_distance")) {
            auto const &v=j.at("sun_shadow_near_distance");
            if(!v.is_number())
                throw std::runtime_error("sun_shadow_near_distance must be numeric (2..128)");
            double const distance=v.get<double>();
            if(!std::isfinite(distance) || distance<2.0 || distance>128.0)
                throw std::runtime_error("sun_shadow_near_distance must be between 2 and 128");
            rv->sun_shadow_near_distance=static_cast<float>(distance);
        }
        if(j.contains("sun_shadow_far_distance")) {
            auto const &v=j.at("sun_shadow_far_distance");
            if(!v.is_number())
                throw std::runtime_error("sun_shadow_far_distance must be numeric (4..512)");
            double const distance=v.get<double>();
            if(!std::isfinite(distance) || distance<4.0 || distance>512.0)
                throw std::runtime_error("sun_shadow_far_distance must be between 4 and 512");
            rv->sun_shadow_far_distance=static_cast<float>(distance);
        }
        if(rv->sun_shadow_far_distance<=rv->sun_shadow_near_distance)
            throw std::runtime_error("sun_shadow_far_distance must exceed sun_shadow_near_distance");
        if(j.contains("sun_max_distance")) {
            auto const &v=j.at("sun_max_distance");
            if(!v.is_number())
                throw std::runtime_error("sun_max_distance must be numeric (0 or 4..512)");
            double const distance=v.get<double>();
            if(!std::isfinite(distance) || distance<0.0 || distance>512.0 ||
               (distance>0.0 && distance<4.0))
                throw std::runtime_error("sun_max_distance must be 0 (unlimited) or between 4 and 512");
            rv->sun_max_distance=static_cast<float>(distance);
        }
        if(rv->sun_max_distance>0.0f &&
           rv->sun_max_distance<=rv->sun_shadow_near_distance)
            throw std::runtime_error("sun_max_distance must exceed sun_shadow_near_distance");
        if(j.contains("sun_shadow_resolution")) {
            auto const &v = j.at("sun_shadow_resolution");
            if(!v.is_number_integer()) {
                throw std::runtime_error(
                    "sun_shadow_resolution must be an integer between 64 and 4096");
            }
            auto const pixels = v.get<long long>();
            if(pixels < 64 || pixels > 4096) {
                throw std::runtime_error(
                    "sun_shadow_resolution must be between 64 and 4096");
            }
            rv->sun_shadow_resolution = static_cast<unsigned int>(pixels);
        }
        if(j.contains("sun_shadow_far_resolution")) {
            auto const &v=j.at("sun_shadow_far_resolution");
            if(!v.is_number_integer())
                throw std::runtime_error(
                    "sun_shadow_far_resolution must be 0 or an integer from 64 to 4096");
            auto const pixels=v.get<long long>();
            if(pixels!=0 && (pixels<64 || pixels>4096))
                throw std::runtime_error(
                    "sun_shadow_far_resolution must be 0 or between 64 and 4096");
            rv->sun_shadow_far_resolution=static_cast<unsigned int>(pixels);
        }
        auto parse_sun_pcf_radius=[&](char const *name,float &out) {
            if(!j.contains(name))return;
            auto const &v=j.at(name);
            if(!v.is_number())
                throw std::runtime_error(std::string(name)+
                                         " must be numeric (0..4)");
            double const radius=v.get<double>();
            if(!std::isfinite(radius) || radius<0.0 || radius>4.0)
                throw std::runtime_error(std::string(name)+
                                         " must be between 0 and 4");
            out=static_cast<float>(radius);
        };
        parse_sun_pcf_radius("sun_shadow_near_pcf_radius",
                             rv->sun_shadow_near_pcf_radius);
        parse_sun_pcf_radius("sun_shadow_far_pcf_radius",
                             rv->sun_shadow_far_pcf_radius);
        auto parse_sun_intensity_pcf=[&](char const *name,float &out,
                                         float min_value,float max_value) {
            if(!j.contains(name))return;
            auto const &v=j.at(name);
            if(!v.is_number())
                throw std::runtime_error(std::string(name)+" must be numeric");
            double const n=v.get<double>();
            if(!std::isfinite(n) || n<min_value || n>max_value)
                throw std::runtime_error(std::string(name)+
                                         " must be within its documented range");
            out=static_cast<float>(n);
        };
        parse_sun_intensity_pcf("experimental_sun_pcf_intensity_softness",
                                rv->experimental_sun_pcf_intensity_softness,
                                0.0f,2.0f);
        parse_sun_intensity_pcf("experimental_sun_pcf_intensity_reference",
                                rv->experimental_sun_pcf_intensity_reference,
                                0.05f,16.0f);
        // Old packs may still ship enable_sprite_decals. Prefer the new
        // enable_decals when both keys are supplied.
        if(j.contains("enable_decals")) {
            j.at("enable_decals").get_to(rv->enable_decals);
        }
        else if(j.contains("enable_sprite_decals")) {
            j.at("enable_sprite_decals").get_to(rv->enable_decals);
        }
        if(j.contains("enable_mesh_collision")) {
            j.at("enable_mesh_collision").get_to(rv->enable_mesh_collision);
        }
        if(j.contains("use_vanilla_renderer")) {
            j.at("use_vanilla_renderer").get_to(rv->use_vanilla_renderer);
        }

        if(j.contains("data_path")) {
            j.at("data_path").get_to(rv->data_path);
        }

        if(j.contains("log_path")) {
            auto const &em = j["log_path"];
            if(!em.is_null()) {
                rv->log_path = em;
            }
        }

        if(j.contains("vram_texture_preload_size")) {
            j.at("vram_texture_preload_size").get_to(rv->vram_texture_preload_size);
        }
    }
    catch(std::exception const &e) {
        LOG_WARNING("Failed to load configuration file: ", e.what());
        report_warning_message(
            str(fmt("JkGfxMod could not load the jkgm.json configuration file. This session "
                    "will use the default options.\n\nDetails: ",
                    e.what())));
    }

    return rv;
}
