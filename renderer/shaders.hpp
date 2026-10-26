#pragma once

#include "base/filesystem.hpp"
#include "base/span.hpp"
#include "gpu_3do_preview.hpp"
#include "glutil/program.hpp"
#include <vector>

namespace jkgm {
    class opengl_shader {
    protected:
        gl::program the_program;

        opengl_shader(std::string const &name,
                      fs::path const &vertex_part,
                      fs::path const &fragment_part);

    public:
        opengl_shader(opengl_shader const &) = delete;
        opengl_shader(opengl_shader &&) = delete;

        opengl_shader &operator=(opengl_shader const &) = delete;
        opengl_shader &operator=(opengl_shader &&) = delete;

        virtual ~opengl_shader() = default;

        void use_program();
    };

    namespace shaders {

        // Shared forward-sun state for native transparent triangles and
        // GPU-cached 3DO translucency. Row vectors map world XYZ to shadow UV.
        struct translucent_sun_uniforms {
            gl::uniform_location_id enabled,maps_ready,dynamic_count;
            gl::uniform_location_id direction,color,split,bias_scale;
            gl::uniform_location_id near_depth,far_depth,dynamic_depth;
            std::vector<gl::uniform_location_id> x,y,z,rect;
            explicit translucent_sun_uniforms(gl::program_view program);
            void set_state(bool active,bool ready,int dynamic,
                           point<3,float> sun_dir,point<4,float> sun_color,
                           float split_distance,float receiver_bias);
            void set_map(int slot,point<4,float> row_x,point<4,float> row_y,
                         point<4,float> row_z,point<4,float> tile_rect);
        };

        class menu_shader : public opengl_shader {
        private:
            gl::uniform_location_id tex;

        public:
            explicit menu_shader(fs::path const &data_root, bool fullscreen = false);

            void set_menu_sampler(int sampler);
        };

        class game_pass_shader : public opengl_shader {
        private:
            gl::uniform_location_id screen_resolution;

            gl::uniform_location_id features;
            gl::uniform_location_id blend_mode;
            gl::uniform_location_id flare_mode;
            gl::uniform_location_id post_exempt_mode;
            gl::uniform_location_id scene_depth;
            gl::uniform_location_id fluid_thickness;

            gl::uniform_location_id albedo_map;
            gl::uniform_location_id albedo_map2;
            gl::uniform_location_id albedo_transition;
            gl::uniform_location_id albedo_factor;

            gl::uniform_location_id emissive_map;
            gl::uniform_location_id emissive_factor;
            gl::uniform_location_id albedo_uv_offset;
            gl::uniform_location_id emissive_uv_offset;
            gl::uniform_location_id normal_map;
            gl::uniform_location_id normal_map2;
            gl::uniform_location_id normal_uv_offset;
            gl::uniform_location_id normal_uv_offset2;
            gl::uniform_location_id normal_params;

            gl::uniform_location_id alpha_cutoff;

            gl::uniform_location_id displacement_map;
            gl::uniform_location_id displacement_factor;

            gl::uniform_location_id fog_params;
            gl::uniform_location_id fog_color;

            gl::uniform_location_id surface_map;
            gl::uniform_location_id env_map;
            gl::uniform_location_id env_map2;
            gl::uniform_location_id env_transition;
            gl::uniform_location_id reflection_params;
            gl::uniform_location_id env_params;
            gl::uniform_location_id sun_direction;
            gl::uniform_location_id sun_color_intensity;

            gl::uniform_location_id camera_right;
            gl::uniform_location_id camera_forward;
            gl::uniform_location_id camera_up;
            gl::uniform_location_id view_params;
            // Forward-only translucent MAT spotlight receivers.
            gl::uniform_location_id translucent_spotlight_count;
            gl::uniform_location_id translucent_spotlight_resolution;
            gl::uniform_location_id translucent_spotlight_camera_origin;
            gl::uniform_location_id translucent_spotlight_camera_right;
            gl::uniform_location_id translucent_spotlight_camera_forward;
            gl::uniform_location_id translucent_spotlight_camera_up;
            gl::uniform_location_id translucent_shadow_slot;
            gl::uniform_location_id translucent_shadow_image;
            gl::uniform_location_id translucent_shadow_near_range;
            std::vector<gl::uniform_location_id> translucent_spotlight_images;
            std::vector<gl::uniform_location_id> translucent_spotlight_origins;
            std::vector<gl::uniform_location_id> translucent_spotlight_rights;
            std::vector<gl::uniform_location_id> translucent_spotlight_ups;
            std::vector<gl::uniform_location_id> translucent_spotlight_forwards;
            std::vector<gl::uniform_location_id> translucent_spotlight_intensities;
            std::vector<gl::uniform_location_id> translucent_spotlight_bounds;

        public:
            translucent_sun_uniforms translucent_sun;
        protected:
            game_pass_shader(std::string const &name,
                             fs::path const &vertex_part,
                             fs::path const &fragment_part);

        public:
            void set_screen_resolution(size<2, float> res);

            void set_features(point<4, float> value);
            // 0 normal, 1 additive, 2 multiply, 3 fluid. Absent in opaque.
            void set_blend_mode(int mode);
            void set_fluid_thickness(float absorption);
            void set_flare_mode(bool enabled);
            // 0=ordinary rendering, 1=post-exempt world, 2=post-exempt gun.
            void set_post_exempt_mode(int mode);
            void set_scene_depth_sampler(int sampler);

            void set_albedo_map_sampler(int sampler);
            void set_albedo_transition(int sampler, point<4, float> params);
            void set_albedo_factor(color factor);

            void set_emissive_map_sampler(int sampler);
            void set_emissive_factor(color_rgb factor);
            void set_uv_offsets(point<2, float> albedo, point<2, float> emissive);
            void set_normal_map_samplers(int first, int second);
            // x/y = strengths, z/w = has first/second layer.
            void set_normal_params(point<4, float> params,
                                   point<2, float> scroll1,
                                   point<2, float> scroll2);

            void set_alpha_cutoff(float cutoff);

            void set_displacement_map_sampler(int sampler);
            void set_surface_map_sampler(int sampler);
            void set_env_map_sampler(int sampler);
            void set_env_transition(int sampler, point<4, float> params);

            // reflection = (strength, roughness, metalness, has surface map)
            // env        = (has environment image, unused, unused, unused)
            void set_reflection(point<4, float> reflection, point<4, float> env);

            // The engine's camera: its axes in world space (w = 1 if available, 0 if not), and
            // view = (tan of half the horizontal field of view, unused, unused, unused)
            // Direction in view-space (toward sun), with w=0 when camera invalid.
            // Color intensity is linear RGB plus brightness in w.
            void set_sun(point<4, float> direction, point<4, float> color_intensity);
            void set_camera(point<4, float> right,
                            point<4, float> forward,
                            point<4, float> up,
                            point<4, float> view);
            void set_displacement_factor(float factor);

            // params = (enabled 0/1, start, end, opacity)
            void set_fog(point<4, float> params, color_rgb color);
            void set_translucent_spotlight_count(int count);
            void set_translucent_shadow(int slot,float near_plane,float far_plane);
            void set_translucent_spotlight_camera(
                point<4,float> origin,point<4,float> right,
                point<4,float> forward,point<4,float> up,
                point<2,float> resolution);
            void set_translucent_spotlight(int slot,
                point<4,float> origin,point<4,float> right,
                point<4,float> up,point<4,float> forward,
                point<2,float> strengths,point<4,float> bounds);
        };

        // Isolated first GPU 3DO mesh test: independent of old D3D vertex format.
        class gpu_3do_preview_shader : public opengl_shader {
            gl::uniform_location_id view_right,view_forward,view_up,view_origin;
            gl::uniform_location_id projection;
            gl::uniform_location_id albedo_sampler,emissive_sampler,uv_scale,has_texture,
                                    has_emissive,transparent_pass,blend_mode,alpha_mode,alpha_cutoff,
                                    native_face_opacity,albedo_factor,emissive_factor,
                                    albedo_scroll,emissive_scroll,fog_params,fog_color,
                                    emissive_only;
            gl::uniform_location_id jk_color_filter,jk_color_tint,jk_color_add_fade;
            gl::uniform_location_id colormap_tint;
            gl::uniform_location_id specular_params,normal_specular_params;
            gl::uniform_location_id dynamic_light_specular_enabled;
            gl::uniform_location_id specular_surface_sampler,specular_normal_sampler;
            gl::uniform_location_id specular_normal2_sampler,specular_environment_sampler;
            gl::uniform_location_id specular_environment2_sampler;
            gl::uniform_location_id albedo2_sampler,albedo_transition_info;
            gl::uniform_location_id scene_depth_sampler,advanced_material_params;
            gl::uniform_location_id post_exempt_pass;
            gl::uniform_location_id world_lighting_exempt;
            gl::uniform_location_id decal_light_texture,decal_occlusion;
            gl::uniform_location_id decal_receiver_lighting;
            gl::uniform_location_id displacement_sampler,displacement_factor;
            gl::uniform_location_id environment_info;
            gl::uniform_location_id camera_right_axis,camera_forward_axis,camera_up_axis;
            gl::uniform_location_id sun_direction,sun_color_intensity;
            gl::uniform_location_id normal_specular_scroll,normal_specular_scroll2,local_eye;
            gl::uniform_location_id translucent_spotlight_count;
            gl::uniform_location_id translucent_spotlight_resolution;
            gl::uniform_location_id translucent_spotlight_camera_origin;
            gl::uniform_location_id translucent_spotlight_camera_right;
            gl::uniform_location_id translucent_spotlight_camera_forward;
            gl::uniform_location_id translucent_spotlight_camera_up;
            gl::uniform_location_id translucent_shadow_slot;
            gl::uniform_location_id translucent_shadow_image;
            gl::uniform_location_id translucent_shadow_near_range;
            std::vector<gl::uniform_location_id> translucent_spotlight_images;
            std::vector<gl::uniform_location_id> translucent_spotlight_origins;
            std::vector<gl::uniform_location_id> translucent_spotlight_rights;
            std::vector<gl::uniform_location_id> translucent_spotlight_ups;
            std::vector<gl::uniform_location_id> translucent_spotlight_forwards;
            std::vector<gl::uniform_location_id> translucent_spotlight_intensities;
            std::vector<gl::uniform_location_id> translucent_spotlight_bounds;
        public:
            translucent_sun_uniforms translucent_sun;
        private:
            gl::uniform_location_id ambient_light,attenuation_min,num_lights,
                                    effective_light_mode;
            std::vector<gl::uniform_location_id> light_position_radius,light_intensity;
        public:
            explicit gpu_3do_preview_shader(fs::path const &data_root);
            void set_view_matrix(point<4,float> right,point<4,float> forward,
                                 point<4,float> up,point<4,float> origin);
            void set_projection(point<4,float> projection_params);
            void set_surface(int albedo_source,float width,float height,bool transparent,
                             int blend,int alpha_policy,float cutoff,float face_opacity,
                             point<4,float> tint,point<2,float> scroll,
                             bool emissive_only);
            void set_emissive(bool textured,point<4,float> intensity,
                              point<2,float> scroll);
            void set_fog(point<4,float> params,point<4,float> color);
            void set_lighting(gpu_3do_instance const &instance);
            void set_colormap_tint(std::array<float,3> const &tint);
            void set_local_eye(std::array<float,3> const &eye);
            void set_dynamic_light_specular(bool enabled);
            void set_point_specular(point<4,float> material,
                                    point<4,float> normal,
                                    point<2,float> normal_scroll,
                                    point<2,float> normal_scroll2);
            void set_environment(bool from,bool to,float weight,int mode);
            void set_albedo_transition(int source2,float weight);
            void set_scene_depth(int sampler);
            void set_translucent_spotlight_count(int count);
            void set_translucent_shadow(int slot,float near_plane,float far_plane);
            void set_translucent_spotlight_camera(
                point<4,float> origin,point<4,float> right,
                point<4,float> forward,point<4,float> up,
                point<2,float> resolution);
            void set_translucent_spotlight(int slot,
                point<4,float> origin,point<4,float> right,
                point<4,float> up,point<4,float> forward,
                point<2,float> strengths,point<4,float> bounds);
            void set_post_exempt(bool enabled);
            void set_world_lighting_exempt(bool enabled);
            void set_decal_receiver_lighting(bool enabled,int light,int ao);
            void set_advanced_material(bool clipped,float fluid_thickness);
            void set_parallax(float factor);
            void set_sun(point<4,float> direction,point<4,float> color_intensity);
            void set_camera_axes(std::array<float,3> const &right,
                                 std::array<float,3> const &forward,
                                 std::array<float,3> const &up);
            void set_jk_color_effects(point<4,float> filter,point<4,float> tint,
                                      point<4,float> add_fade);
        };

        class game_opaque_pass_shader : public game_pass_shader {
        public:
            explicit game_opaque_pass_shader(fs::path const &data_root);
        };

        class game_post_ssao_shader : public opengl_shader {
        public:
            static constexpr int num_kernel_samples = 16;

        private:
            gl::uniform_location_id normal_image;
            gl::uniform_location_id depth_image;
            gl::uniform_location_id noise_image;

            std::vector<gl::uniform_location_id> samples;

        public:
            explicit game_post_ssao_shader(fs::path const &data_root);

            void set_normal_sampler(int sampler);
            void set_depth_sampler(int sampler);
            void set_noise_sampler(int sampler);

            void set_samples(span<point<3, float> const> values);
        };

        // Optional depth-only spotlight pass, constructed only when enabled.
        class spotlight_shadow_depth_shader : public opengl_shader {
            gl::uniform_location_id shadow_origin,shadow_right,shadow_up;
            gl::uniform_location_id shadow_forward,shadow_limits;
            gl::uniform_location_id shadow_orthographic;
            gl::uniform_location_id shadow_model_right,shadow_model_forward;
            gl::uniform_location_id shadow_model_up,shadow_model_origin;
            gl::uniform_location_id shadow_use_alpha,shadow_alpha_texture;
            gl::uniform_location_id shadow_alpha_texture_next;
            gl::uniform_location_id shadow_alpha_uv_scale,shadow_alpha_uv_scroll;
            gl::uniform_location_id shadow_alpha_transition,shadow_alpha_cutoff;
            gl::uniform_location_id shadow_alpha_factor;
        public:
            explicit spotlight_shadow_depth_shader(fs::path const &data_root);
            void set_light(point<3,float> origin,point<3,float> right,
                           point<3,float> up,point<3,float> forward,
                           point<4,float> limits);
            void set_orthographic_light(point<3,float> origin,
                                        point<3,float> right,
                                        point<3,float> up,
                                        point<3,float> forward,
                                        point<4,float> limits);
            void set_model(point<3,float> right,point<3,float> forward,
                           point<3,float> up,point<3,float> origin);
            void set_shadow_alpha(bool enabled,point<2,float> uv_scale,
                                  point<2,float> uv_scroll,float cutoff,
                                  float factor,bool has_next,float progress);
        };

        class game_post_opaque_composite_shader : public opengl_shader {
        private:
            gl::uniform_location_id color_image;
            gl::uniform_location_id emissive_image;
            gl::uniform_location_id occlusion_image;
            gl::uniform_location_id depth_image;
            gl::uniform_location_id fill_tjunction_gaps;
            gl::uniform_location_id fog_params;
            gl::uniform_location_id fog_color;
            gl::uniform_location_id normal_image;
            gl::uniform_location_id camera_origin,camera_right,
                                    camera_forward,camera_up;
            gl::uniform_location_id spotlight_count;
            gl::uniform_location_id spotlight_shadow_slot;
            gl::uniform_location_id spotlight_shadow_image;
            gl::uniform_location_id spotlight_shadow_near_range;
            gl::uniform_location_id sun_lighting_active,sun_shadow_active,sun_shadow_image;
            gl::uniform_location_id sun_shadow_direction,sun_shadow_color_intensity;
            gl::uniform_location_id sun_shadow_origin,sun_shadow_right;
            gl::uniform_location_id sun_shadow_up,sun_shadow_forward;
            gl::uniform_location_id sun_shadow_limits;
            gl::uniform_location_id sun_shadow_far_image,sun_shadow_far_origin;
            gl::uniform_location_id sun_shadow_far_right,sun_shadow_far_up;
            gl::uniform_location_id sun_shadow_far_forward,sun_shadow_far_limits;
            gl::uniform_location_id sun_shadow_split,sun_shadow_far_soft_filter;
            gl::uniform_location_id sun_shadow_receiver_bias_scale;
            gl::uniform_location_id sun_dynamic_count,sun_dynamic_image;
            std::vector<gl::uniform_location_id> sun_dynamic_atlas_rects;
            std::vector<gl::uniform_location_id> sun_dynamic_origin;
            std::vector<gl::uniform_location_id> sun_dynamic_right;
            std::vector<gl::uniform_location_id> sun_dynamic_up;
            std::vector<gl::uniform_location_id> sun_dynamic_forward;
            std::vector<gl::uniform_location_id> sun_dynamic_limits;
            std::vector<gl::uniform_location_id> spotlight_images;
            std::vector<gl::uniform_location_id> spotlight_origins;
            std::vector<gl::uniform_location_id> spotlight_rights;
            std::vector<gl::uniform_location_id> spotlight_ups;
            std::vector<gl::uniform_location_id> spotlight_forwards;
            std::vector<gl::uniform_location_id> spotlight_strengths;
            std::vector<gl::uniform_location_id> spotlight_screen_bounds;

        public:
            explicit game_post_opaque_composite_shader(fs::path const &data_root);

            void set_color_sampler(int sampler);
            void set_emissive_sampler(int sampler);
            void set_occlusion_sampler(int sampler);
            void set_depth_sampler(int sampler);
            void set_fill_tjunction_gaps(bool enabled);
            void set_fog(point<4,float> params,color_rgb fog);
            void set_projector_camera(point<4,float> origin,
                                      point<4,float> right,point<4,float> forward,
                                      point<4,float> up);
            void set_spotlight_count(int count);
            void set_spotlight_shadow(int slot,float near_plane,float far_plane);
            void set_sun_shadow(bool lighting_enabled,bool shadows_enabled,
                                point<3,float> direction,
                                point<4,float> color_intensity,
                                point<3,float> origin,point<3,float> right,
                                point<3,float> up,point<3,float> forward,
                                point<3,float> limits);
            void set_sun_shadow_far(point<3,float> origin,point<3,float> right,
                                    point<3,float> up,point<3,float> forward,
                                    point<3,float> limits,float split);
            void set_sun_shadow_far_soft_filter(bool soft);
            void set_sun_shadow_receiver_bias_scale(float scale);
            void set_sun_dynamic_count(int count);
            void set_sun_dynamic_atlas_rect(int slot,point<4,float> rect);
            void set_sun_dynamic(int slot,point<3,float> origin,
                                 point<3,float> right,point<3,float> up,
                                 point<3,float> forward,point<3,float> limits);
            void set_spotlight(int slot,point<4,float> origin,
                               point<4,float> right,point<4,float> up,
                               point<4,float> forward,
                               point<2,float> intensities,
                               point<4,float> screen_bounds);
        };

        class game_transparency_pass_shader : public game_pass_shader {
        public:
            explicit game_transparency_pass_shader(fs::path const &data_root);
        };

        class post_box4_shader : public opengl_shader {
        private:
            gl::uniform_location_id fbuf_image;
            gl::uniform_location_id normal_image;
            gl::uniform_location_id depth_image;

        public:
            explicit post_box4_shader(fs::path const &data_root);

            void set_fbuf_sampler(int sampler);
            void set_normal_sampler(int sampler);
            void set_depth_sampler(int sampler);
        };

        class post_gauss_shader : public opengl_shader {
        public:
            using opengl_shader::opengl_shader;

            virtual void set_fbuf_sampler(int sampler) = 0;
            virtual void set_blur_direction(direction<2, int> dir) = 0;
        };

        class post_gauss3_shader : public post_gauss_shader {
        private:
            gl::uniform_location_id fbuf_image;
            gl::uniform_location_id blur_direction;

        public:
            explicit post_gauss3_shader(fs::path const &data_root);

            void set_fbuf_sampler(int sampler) override;
            void set_blur_direction(direction<2, int> dir) override;
        };

        class post_gauss7_shader : public post_gauss_shader {
        private:
            gl::uniform_location_id fbuf_image;
            gl::uniform_location_id blur_direction;

        public:
            explicit post_gauss7_shader(fs::path const &data_root);

            void set_fbuf_sampler(int sampler) override;
            void set_blur_direction(direction<2, int> dir) override;
        };

        class post_gauss9_shader : public post_gauss_shader {
        private:
            gl::uniform_location_id fbuf_image;
            gl::uniform_location_id blur_direction;

        public:
            explicit post_gauss9_shader(fs::path const &data_root);

            void set_fbuf_sampler(int sampler) override;
            void set_blur_direction(direction<2, int> dir) override;
        };

        class post_gauss15_shader : public post_gauss_shader {
        private:
            gl::uniform_location_id fbuf_image;
            gl::uniform_location_id blur_direction;

        public:
            explicit post_gauss15_shader(fs::path const &data_root);

            void set_fbuf_sampler(int sampler) override;
            void set_blur_direction(direction<2, int> dir) override;
        };

        class post_low_pass_shader : public opengl_shader {
        private:
            gl::uniform_location_id fbuf_image;
            gl::uniform_location_id exposure_image,exposure_enabled;
            gl::uniform_location_id hdr_exposure_compensation;

        public:
            explicit post_low_pass_shader(fs::path const &data_root);

            void set_fbuf_sampler(int sampler);
            void set_exposure(int sampler,bool adaptive,float compensation_ev);
        };

        class post_scale_shader : public opengl_shader {
        private:
            gl::uniform_location_id fbuf_image;

        public:
            explicit post_scale_shader(fs::path const &data_root);

            void set_fbuf_sampler(int sampler);
        };

        class post_spatial_blur_shader : public opengl_shader {
        private:
            gl::uniform_location_id source_image;
            gl::uniform_location_id depth_image;
            gl::uniform_location_id blur_axis;
            gl::uniform_location_id screen_size;
            gl::uniform_location_id distance_params;
            gl::uniform_location_id peripheral_params;

        public:
            explicit post_spatial_blur_shader(fs::path const &data_root);
            void set_source_sampler(int sampler);
            void set_depth_sampler(int sampler);
            void set_blur_axis(int axis);
            void set_screen_size(size<2, float> dims);
            void set_distance_params(point<4, float> params);
            void set_peripheral_params(point<4, float> params);
        };

        class post_frame_blend_shader : public opengl_shader {
        private:
            gl::uniform_location_id current_image;
            gl::uniform_location_id history_image;
            gl::uniform_location_id blend_strength;
            gl::uniform_location_id retro_palette_sampler,retro_palette_enabled;

        public:
            explicit post_frame_blend_shader(fs::path const &data_root);
            void set_current_sampler(int sampler);
            void set_history_sampler(int sampler);
            void set_blend_strength(float strength);
            void set_retro_palette(int sampler,bool enabled);
        };

        // One-pixel GPU reduction / temporal eye-adaptation shader.
        class post_auto_exposure_shader : public opengl_shader {
            gl::uniform_location_id scene_image, previous_exposure, exposure_params;
        public:
            explicit post_auto_exposure_shader(fs::path const &data_root);
            void set_inputs(int scene_sampler,int history_sampler);
            // sensitivity, dt seconds, has_history, reserved
            void set_params(point<4,float> params);
        };

        class post_to_srgb_shader : public opengl_shader {
        public:
            static constexpr int num_bloom_layers = 5;

        private:
            gl::uniform_location_id fbuf_image;
            gl::uniform_location_id grain_params;
            gl::uniform_location_id vignette_params;
            gl::uniform_location_id brightness;
            gl::uniform_location_id contrast;
            gl::uniform_location_id saturation;
            gl::uniform_location_id exposure_image,exposure_enabled;
            gl::uniform_location_id hdr_enabled,hdr_exposure_compensation,hdr_shadow_softness;
            gl::uniform_location_id hdr_color_crispness;
            std::vector<gl::uniform_location_id> bloom_fbuf;
            std::vector<gl::uniform_location_id> bloom_weight;

        public:
            explicit post_to_srgb_shader(fs::path const &data_root);

            void set_fbuf_sampler(int sampler);
            void set_hdr(int sampler,bool hdr,bool adaptive,float compensation_ev,
                         float shadow_softness,float color_crispness);
            void set_bloom_samplers(span<int const> samplers);
            void set_bloom_weights(span<float const> weights);

            // params = (amount, size in pixels, frame seed, unused)
            void set_film_grain(point<4, float> params);

            // params = (red, green, blue, strength)
            void set_vignette(point<4, float> params);

            void set_color_filter(float brightness_value, float contrast_value, float saturation_value);
        };
    }
}
