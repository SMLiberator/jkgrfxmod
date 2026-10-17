#pragma once

#include "base/filesystem.hpp"
#include "base/span.hpp"
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
        class menu_shader : public opengl_shader {
        private:
            gl::uniform_location_id tex;

        public:
            explicit menu_shader(fs::path const &data_root);

            void set_menu_sampler(int sampler);
        };

        class game_pass_shader : public opengl_shader {
        private:
            gl::uniform_location_id screen_resolution;

            gl::uniform_location_id features;

            gl::uniform_location_id albedo_map;
            gl::uniform_location_id albedo_factor;

            gl::uniform_location_id emissive_map;
            gl::uniform_location_id emissive_factor;

            gl::uniform_location_id alpha_cutoff;

            gl::uniform_location_id displacement_map;
            gl::uniform_location_id displacement_factor;

            gl::uniform_location_id fog_params;
            gl::uniform_location_id fog_color;

            gl::uniform_location_id surface_map;
            gl::uniform_location_id env_map;
            gl::uniform_location_id reflection_params;
            gl::uniform_location_id env_params;

            gl::uniform_location_id camera_right;
            gl::uniform_location_id camera_forward;
            gl::uniform_location_id camera_up;
            gl::uniform_location_id view_params;

        protected:
            game_pass_shader(std::string const &name,
                             fs::path const &vertex_part,
                             fs::path const &fragment_part);

        public:
            void set_screen_resolution(size<2, float> res);

            void set_features(point<4, float> value);

            void set_albedo_map_sampler(int sampler);
            void set_albedo_factor(color factor);

            void set_emissive_map_sampler(int sampler);
            void set_emissive_factor(color_rgb factor);

            void set_alpha_cutoff(float cutoff);

            void set_displacement_map_sampler(int sampler);
            void set_surface_map_sampler(int sampler);
            void set_env_map_sampler(int sampler);

            // reflection = (strength, roughness, metalness, has surface map)
            // env        = (has environment image, unused, unused, unused)
            void set_reflection(point<4, float> reflection, point<4, float> env);

            // The engine's camera: its axes in world space (w = 1 if available, 0 if not), and
            // view = (tan of half the horizontal field of view, unused, unused, unused)
            void set_camera(point<4, float> right,
                            point<4, float> forward,
                            point<4, float> up,
                            point<4, float> view);
            void set_displacement_factor(float factor);

            // params = (enabled 0/1, start, end, opacity)
            void set_fog(point<4, float> params, color_rgb color);
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

        class game_post_opaque_composite_shader : public opengl_shader {
        private:
            gl::uniform_location_id color_image;
            gl::uniform_location_id emissive_image;
            gl::uniform_location_id occlusion_image;

        public:
            explicit game_post_opaque_composite_shader(fs::path const &data_root);

            void set_color_sampler(int sampler);
            void set_emissive_sampler(int sampler);
            void set_occlusion_sampler(int sampler);
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

        public:
            explicit post_low_pass_shader(fs::path const &data_root);

            void set_fbuf_sampler(int sampler);
        };

        class post_scale_shader : public opengl_shader {
        private:
            gl::uniform_location_id fbuf_image;

        public:
            explicit post_scale_shader(fs::path const &data_root);

            void set_fbuf_sampler(int sampler);
        };

        class post_frame_blend_shader : public opengl_shader {
        private:
            gl::uniform_location_id current_image;
            gl::uniform_location_id history_image;
            gl::uniform_location_id blend_strength;

        public:
            explicit post_frame_blend_shader(fs::path const &data_root);
            void set_current_sampler(int sampler);
            void set_history_sampler(int sampler);
            void set_blend_strength(float strength);
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
            std::vector<gl::uniform_location_id> bloom_fbuf;
            std::vector<gl::uniform_location_id> bloom_weight;

        public:
            explicit post_to_srgb_shader(fs::path const &data_root);

            void set_fbuf_sampler(int sampler);
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
