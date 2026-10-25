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

        if(j.contains("enable_blur")) {
            j.at("enable_blur").get_to(rv->enable_blur);
        }
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
        if(j.contains("enable_dynamic_light_specular")) {
            j.at("enable_dynamic_light_specular").get_to(
                rv->enable_dynamic_light_specular);
        }

        if(j.contains("enable_ssao")) {
            j.at("enable_ssao").get_to(rv->enable_ssao);
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
        if(j.contains("profile_audio")) {
            j.at("profile_audio").get_to(rv->profile_audio);
        }
        if(j.contains("profile_auto_exposure")) {
            j.at("profile_auto_exposure").get_to(rv->profile_auto_exposure);
        }
        if(j.contains("optimize_hud_uploads")) {
            j.at("optimize_hud_uploads").get_to(rv->optimize_hud_uploads);
        }
        if(j.contains("use_model_cache")) {
            j.at("use_model_cache").get_to(rv->use_model_cache);
        }
        if(j.contains("mesh_revision_tracking")) {
            j.at("mesh_revision_tracking").get_to(rv->mesh_revision_tracking);
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
        if(j.contains("enable_sun_shadows")) {
            j.at("enable_sun_shadows").get_to(rv->enable_sun_shadows);
        }
        if(j.contains("enable_projected_sun_shadows")) {
            j.at("enable_projected_sun_shadows").get_to(
                rv->enable_projected_sun_shadows);
        }
        if(j.contains("projected_sun_mask_scale")) {
            auto const &scale=j.at("projected_sun_mask_scale");
            if(!scale.is_number()) {
                throw std::runtime_error(
                    "projected_sun_mask_scale must be a number between 0.25 and 1");
            }
            float value=scale.get<float>();
            if(!std::isfinite(value) || value<0.25f || value>1.0f) {
                throw std::runtime_error(
                    "projected_sun_mask_scale must be between 0.25 and 1");
            }
            rv->projected_sun_mask_scale=value;
        }
        if(j.contains("enable_sun_dynamic_shadows")) {
            j.at("enable_sun_dynamic_shadows").get_to(rv->enable_sun_dynamic_shadows);
        }
        if(j.contains("sun_shadow_far_soft_filter")) {
            j.at("sun_shadow_far_soft_filter").get_to(
                rv->sun_shadow_far_soft_filter);
        }
        if(j.contains("sun_shadow_resolution")) {
            auto const &v = j.at("sun_shadow_resolution");
            if(!v.is_number_integer()) {
                throw std::runtime_error(
                    "sun_shadow_resolution must be an integer between 64 and 2048");
            }
            auto const pixels = v.get<long long>();
            if(pixels < 64 || pixels > 2048) {
                throw std::runtime_error(
                    "sun_shadow_resolution must be between 64 and 2048");
            }
            rv->sun_shadow_resolution = static_cast<unsigned int>(pixels);
        }
        if(j.contains("enable_sprite_decals")) {
            j.at("enable_sprite_decals").get_to(rv->enable_sprite_decals);
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
