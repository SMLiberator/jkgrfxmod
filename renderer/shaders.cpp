#include "shaders.hpp"
#include "base/file_stream.hpp"
#include "base/log.hpp"
#include "base/memory_block.hpp"
#include "common/error_reporter.hpp"

namespace jkgm {
    namespace {
        gl::shader compile_shader_from_file(fs::path const &filename, gl::shader_type type)
        {
            diagnostic_context dc(filename.generic_string());
            gl::shader rv(type);

            memory_block mb;

            try {
                auto fs = make_file_input_stream(filename);
                memory_output_block mob(&mb);
                fs->copy_to(&mob);
            }
            catch(std::exception const &e) {
                report_fatal_message(str(fmt("JkGfxMod could not open an essential file and cannot "
                                             "continue.\n\nDetails: Error while opening ",
                                             filename.generic_string(),
                                             "\n",
                                             e.what())));
            }

            gl::shader_source(rv, make_string_span(""), make_span(mb.str()));
            gl::compile_shader(rv);
            if(!gl::get_shader_compile_status(rv)) {
                auto err_str = gl::get_shader_info_log(rv);
                report_fatal_message(str(fmt(
                    "JkGfxMod failed to compile an essential shader. This may have happened "
                    "because your "
                    "graphics device does not support OpenGL 3.3, because the shader file has been "
                    "incorrectly modified, or because of a bug in JkGfxMod.\n\nDetails: Failed to "
                    "compile ",
                    filename.generic_string(),
                    "\n",
                    err_str)));
            }

            return rv;
        }

        void link_program_from_files(std::string const &name,
                                     gl::program_view prog,
                                     fs::path const &vx,
                                     fs::path const &fg)
        {
            auto vx_shader = compile_shader_from_file(vx, gl::shader_type::vertex);
            auto fg_shader = compile_shader_from_file(fg, gl::shader_type::fragment);

            gl::attach_shader(prog, vx_shader);
            gl::attach_shader(prog, fg_shader);

            gl::link_program(prog);

            if(!gl::get_program_link_status(prog)) {
                auto err_str = gl::get_program_info_log(prog);
                report_fatal_message(str(fmt("JkGfxMod failed to link an essential shader. This "
                                             "may have happened because your "
                                             "graphics device does not support OpenGL 3.3, "
                                             "because the shader files have been "
                                             "incorrectly modified, or because of a bug in "
                                             "JkGfxMod.\n\nDetails: Failed to link ",
                                             name,
                                             "\n",
                                             err_str)));
            }
        }
    }
}

jkgm::opengl_shader::opengl_shader(std::string const &name,
                                   fs::path const &vertex_part,
                                   fs::path const &fragment_part)
{
    link_program_from_files(name, the_program, vertex_part, fragment_part);
}

void jkgm::opengl_shader::use_program()
{
    gl::use_program(the_program);
}

jkgm::shaders::menu_shader::menu_shader(fs::path const &data_root)
    : opengl_shader("menu", data_root / "shaders/game.vert", data_root / "shaders/menu.frag")
    , tex(gl::get_uniform_location(the_program, "tex"))
{
}

void jkgm::shaders::menu_shader::set_menu_sampler(int sampler)
{
    gl::set_uniform_integer(tex, sampler);
}

jkgm::shaders::game_pass_shader::game_pass_shader(std::string const &name,
                                                  fs::path const &vertex_part,
                                                  fs::path const &fragment_part)
    : opengl_shader(name, vertex_part, fragment_part)
    , screen_resolution(gl::get_uniform_location(the_program, "screen_resolution"))
    , features(gl::get_uniform_location(the_program, "features"))
    , blend_mode(gl::get_uniform_location(the_program, "blend_mode"))
    , depth_clip(gl::get_uniform_location(the_program, "depth_clip"))
    , flare_mode(gl::get_uniform_location(the_program, "flare_mode"))
    , scene_depth(gl::get_uniform_location(the_program, "scene_depth"))
    , fluid_thickness(gl::get_uniform_location(the_program, "fluid_thickness"))
    , albedo_map(gl::get_uniform_location(the_program, "albedo_map"))
    , albedo_map2(gl::get_uniform_location(the_program, "albedo_map2"))
    , albedo_transition(gl::get_uniform_location(the_program, "albedo_transition"))
    , albedo_factor(gl::get_uniform_location(the_program, "albedo_factor"))
    , emissive_map(gl::get_uniform_location(the_program, "emissive_map"))
    , emissive_factor(gl::get_uniform_location(the_program, "emissive_factor"))
    , albedo_uv_offset(gl::get_uniform_location(the_program, "albedo_uv_offset"))
    , emissive_uv_offset(gl::get_uniform_location(the_program, "emissive_uv_offset"))
    , normal_map(gl::get_uniform_location(the_program, "normal_map"))
    , normal_map2(gl::get_uniform_location(the_program, "normal_map2"))
    , normal_uv_offset(gl::get_uniform_location(the_program, "normal_uv_offset"))
    , normal_uv_offset2(gl::get_uniform_location(the_program, "normal_uv_offset2"))
    , normal_params(gl::get_uniform_location(the_program, "normal_params"))
    , alpha_cutoff(gl::get_uniform_location(the_program, "alpha_cutoff"))
    , displacement_map(gl::get_uniform_location(the_program, "displacement_map"))
    , displacement_factor(gl::get_uniform_location(the_program, "displacement_factor"))
    , fog_params(gl::get_uniform_location(the_program, "fog_params"))
    , fog_color(gl::get_uniform_location(the_program, "fog_color"))
    , surface_map(gl::get_uniform_location(the_program, "surface_map"))
    , env_map(gl::get_uniform_location(the_program, "env_map"))
    , env_map2(gl::get_uniform_location(the_program, "env_map2"))
    , env_transition(gl::get_uniform_location(the_program, "env_transition"))
    , reflection_params(gl::get_uniform_location(the_program, "reflection_params"))
    , env_params(gl::get_uniform_location(the_program, "env_params"))
    , sun_direction(gl::get_uniform_location(the_program, "sun_direction"))
    , sun_color_intensity(gl::get_uniform_location(the_program, "sun_color_intensity"))
    , camera_right(gl::get_uniform_location(the_program, "camera_right"))
    , camera_forward(gl::get_uniform_location(the_program, "camera_forward"))
    , camera_up(gl::get_uniform_location(the_program, "camera_up"))
    , view_params(gl::get_uniform_location(the_program, "view_params"))
{
}

jkgm::shaders::gpu_3do_preview_shader::gpu_3do_preview_shader(fs::path const &root)
    : opengl_shader("gpu_3do_preview",root/"shaders/gpu_3do_preview.vert",
                   root/"shaders/gpu_3do_preview.frag")
    , view_right(gl::get_uniform_location(the_program,"view_right"))
    , view_forward(gl::get_uniform_location(the_program,"view_forward"))
    , view_up(gl::get_uniform_location(the_program,"view_up"))
    , view_origin(gl::get_uniform_location(the_program,"view_origin"))
    , projection(gl::get_uniform_location(the_program,"projection"))
    , albedo_sampler(gl::get_uniform_location(the_program,"albedo_map"))
    , emissive_sampler(gl::get_uniform_location(the_program,"emissive_map"))
    , uv_scale(gl::get_uniform_location(the_program,"uv_scale"))
    , has_texture(gl::get_uniform_location(the_program,"has_texture"))
    , has_emissive(gl::get_uniform_location(the_program,"has_emissive"))
    , transparent_pass(gl::get_uniform_location(the_program,"transparent_pass"))
    , blend_mode(gl::get_uniform_location(the_program,"blend_mode"))
    , emissive_only(gl::get_uniform_location(the_program,"emissive_only"))
    , alpha_mode(gl::get_uniform_location(the_program,"alpha_mode"))
    , alpha_cutoff(gl::get_uniform_location(the_program,"alpha_cutoff"))
    , native_face_opacity(gl::get_uniform_location(the_program,"native_face_opacity"))
    , albedo_factor(gl::get_uniform_location(the_program,"albedo_factor"))
    , emissive_factor(gl::get_uniform_location(the_program,"emissive_factor"))
    , albedo_scroll(gl::get_uniform_location(the_program,"albedo_scroll"))
    , emissive_scroll(gl::get_uniform_location(the_program,"emissive_scroll"))
    , fog_params(gl::get_uniform_location(the_program,"fog_params"))
    , fog_color(gl::get_uniform_location(the_program,"fog_color"))
    , jk_color_filter(gl::get_uniform_location(the_program,"jk_color_filter"))
    , jk_color_tint(gl::get_uniform_location(the_program,"jk_color_tint"))
    , jk_color_add_fade(gl::get_uniform_location(the_program,"jk_color_add_fade"))
    , colormap_tint(gl::get_uniform_location(the_program,"colormap_tint"))
    , specular_params(gl::get_uniform_location(the_program,"specular_params"))
    , specular_surface_sampler(gl::get_uniform_location(the_program,"surface_map"))
    , specular_normal_sampler(gl::get_uniform_location(the_program,"normal_map"))
    , specular_normal2_sampler(gl::get_uniform_location(the_program,"normal_map2"))
    , specular_environment_sampler(gl::get_uniform_location(the_program,"env_map"))
    , specular_environment2_sampler(gl::get_uniform_location(the_program,"env_map2"))
    , albedo2_sampler(gl::get_uniform_location(the_program,"albedo_map2"))
    , albedo_transition_info(gl::get_uniform_location(the_program,"albedo_transition"))
    , scene_depth_sampler(gl::get_uniform_location(the_program,"scene_depth"))
    , advanced_material_params(gl::get_uniform_location(the_program,"advanced_material_params"))
    , displacement_sampler(gl::get_uniform_location(the_program,"displacement_map"))
    , displacement_factor(gl::get_uniform_location(the_program,"displacement_factor"))
    , environment_info(gl::get_uniform_location(the_program,"environment_info"))
    , camera_right_axis(gl::get_uniform_location(the_program,"camera_right_axis"))
    , camera_forward_axis(gl::get_uniform_location(the_program,"camera_forward_axis"))
    , camera_up_axis(gl::get_uniform_location(the_program,"camera_up_axis"))
    , sun_direction(gl::get_uniform_location(the_program,"sun_direction"))
    , sun_color_intensity(gl::get_uniform_location(the_program,"sun_color_intensity"))
    , normal_specular_params(gl::get_uniform_location(the_program,"normal_specular_params"))
    , normal_specular_scroll(gl::get_uniform_location(the_program,"normal_specular_scroll"))
    , normal_specular_scroll2(gl::get_uniform_location(the_program,"normal_specular_scroll2"))
    , local_eye(gl::get_uniform_location(the_program,"local_eye"))
    , ambient_light(gl::get_uniform_location(the_program,"ambient_light"))
    , attenuation_min(gl::get_uniform_location(the_program,"attenuation_min"))
    , num_lights(gl::get_uniform_location(the_program,"num_lights"))
    , effective_light_mode(gl::get_uniform_location(the_program,"effective_light_mode"))
{
    for(unsigned i=0;i<gpu_3do_max_lights;i++) {
        light_position_radius.push_back(gl::get_uniform_location(
            the_program,std::string("light_position_radius[")+std::to_string(i)+"]"));
        light_intensity.push_back(gl::get_uniform_location(
            the_program,std::string("light_intensity[")+std::to_string(i)+"]"));
    }
}
void jkgm::shaders::gpu_3do_preview_shader::set_view_matrix(
    point<4,float> r,point<4,float> f,point<4,float> u,point<4,float> t)
{
    gl::set_uniform_vector(view_right,r);
    gl::set_uniform_vector(view_forward,f);
    gl::set_uniform_vector(view_up,u);
    gl::set_uniform_vector(view_origin,t);
}
void jkgm::shaders::gpu_3do_preview_shader::set_projection(
    point<4,float> p)
{
    gl::set_uniform_vector(projection,p);
}

void jkgm::shaders::gpu_3do_preview_shader::set_surface(
    int albedo_source,float width,float height,bool transparent,
    int blend,int policy,float cutoff,float face_opacity,point<4,float> tint,
    point<2,float> scroll,bool emission_only)
{
    gl::set_uniform_integer(emissive_only,emission_only?1:0);
    gl::set_uniform_integer(albedo_sampler,0);
    gl::set_uniform_integer(has_texture,albedo_source);
    gl::set_uniform_integer(transparent_pass,transparent?1:0);
    gl::set_uniform_integer(blend_mode,blend);
    gl::set_uniform_integer(alpha_mode,policy);
    gl::set_uniform_float(alpha_cutoff,cutoff);
    gl::set_uniform_float(native_face_opacity,face_opacity);
    gl::set_uniform_vector(albedo_factor,tint);
    gl::set_uniform_vector(albedo_scroll,scroll);
    gl::set_uniform_vector(uv_scale,make_point(1.0f/width,1.0f/height));
}
void jkgm::shaders::gpu_3do_preview_shader::set_emissive(
    bool textured,point<4,float> intensity,point<2,float> scroll)
{
    gl::set_uniform_integer(emissive_sampler,1);
    gl::set_uniform_integer(has_emissive,textured?1:0);
    gl::set_uniform_vector(emissive_factor,make_point(get<0>(intensity),
                                                      get<1>(intensity),
                                                      get<2>(intensity)));
    gl::set_uniform_vector(emissive_scroll,scroll);
}
void jkgm::shaders::gpu_3do_preview_shader::set_fog(
    point<4,float> params,point<4,float> color)
{
    gl::set_uniform_vector(fog_params,params);
    gl::set_uniform_vector(fog_color,
        make_point(get<0>(color),get<1>(color),get<2>(color)));
}

void jkgm::shaders::gpu_3do_preview_shader::set_jk_color_effects(
    point<4,float> filter,point<4,float> tint,point<4,float> add_fade)
{
    gl::set_uniform_vector(jk_color_filter,filter);
    gl::set_uniform_vector(jk_color_tint,tint);
    gl::set_uniform_vector(jk_color_add_fade,add_fade);
}

void jkgm::shaders::gpu_3do_preview_shader::set_colormap_tint(
    std::array<float,3> const &tint)
{
    gl::set_uniform_vector(colormap_tint,make_point(tint[0],tint[1],tint[2]));
}

void jkgm::shaders::gpu_3do_preview_shader::set_local_eye(
    std::array<float,3> const &eye)
{
    gl::set_uniform_vector(local_eye,make_point(eye[0],eye[1],eye[2]));
}

void jkgm::shaders::gpu_3do_preview_shader::set_point_specular(
    point<4,float> material,point<4,float> normal,point<2,float> scroll,
    point<2,float> scroll2)
{
    gl::set_uniform_integer(specular_surface_sampler,2);
    gl::set_uniform_integer(specular_normal_sampler,3);
    gl::set_uniform_integer(specular_normal2_sampler,5);
    gl::set_uniform_vector(specular_params,material);
    gl::set_uniform_vector(normal_specular_params,normal);
    gl::set_uniform_vector(normal_specular_scroll,scroll);
    gl::set_uniform_vector(normal_specular_scroll2,scroll2);
}

void jkgm::shaders::gpu_3do_preview_shader::set_environment(
    bool from,bool to,float weight,int mode)
{
    gl::set_uniform_integer(specular_environment_sampler,4);
    gl::set_uniform_integer(specular_environment2_sampler,7);
    gl::set_uniform_vector(environment_info,
        make_point(from?1.0f:0.0f,static_cast<float>(mode),
                   to?1.0f:0.0f,weight));
}
void jkgm::shaders::gpu_3do_preview_shader::set_albedo_transition(
    int source2,float weight)
{
    gl::set_uniform_integer(albedo2_sampler,6);
    gl::set_uniform_vector(albedo_transition_info,
        make_point(source2==1?1.0f:0.0f,weight,weight>0.0f?1.0f:0.0f,0.0f));
}

void jkgm::shaders::gpu_3do_preview_shader::set_scene_depth(int sampler)
{
    gl::set_uniform_integer(scene_depth_sampler,sampler);
}
void jkgm::shaders::gpu_3do_preview_shader::set_advanced_material(
    bool clipped,float thickness)
{
    gl::set_uniform_vector(advanced_material_params,
                           make_point(clipped?1.0f:0.0f,thickness));
}

void jkgm::shaders::gpu_3do_preview_shader::set_parallax(float factor)
{
    gl::set_uniform_integer(displacement_sampler,9);
    gl::set_uniform_float(displacement_factor,factor);
}

void jkgm::shaders::gpu_3do_preview_shader::set_sun(
    point<4,float> direction,point<4,float> color_intensity)
{
    gl::set_uniform_vector(sun_direction,direction);
    gl::set_uniform_vector(sun_color_intensity,color_intensity);
}

void jkgm::shaders::gpu_3do_preview_shader::set_camera_axes(
    std::array<float,3> const &r,std::array<float,3> const &f,
    std::array<float,3> const &u)
{
    gl::set_uniform_vector(camera_right_axis,make_point(r[0],r[1],r[2]));
    gl::set_uniform_vector(camera_forward_axis,make_point(f[0],f[1],f[2]));
    gl::set_uniform_vector(camera_up_axis,make_point(u[0],u[1],u[2]));
}

void jkgm::shaders::gpu_3do_preview_shader::set_lighting(
    gpu_3do_instance const &instance)
{
    gl::set_uniform_float(ambient_light,instance.ambient);
    gl::set_uniform_float(attenuation_min,instance.attenuation);
    gl::set_uniform_integer(num_lights,static_cast<int>(instance.num_lights));
    gl::set_uniform_float(effective_light_mode,instance.effective_light_mode);
    for(unsigned i=0;i<instance.num_lights;i++) {
        auto const &light=instance.lights[i];
        gl::set_uniform_vector(light_position_radius[i],
            make_point(light.local_position[0],light.local_position[1],
                       light.local_position[2],light.radius));
        gl::set_uniform_float(light_intensity[i],light.intensity);
    }
}

jkgm::shaders::game_opaque_pass_shader::game_opaque_pass_shader(fs::path const &data_root)
    : game_pass_shader("game_opaque_pass",
                       data_root / "shaders/game.vert",
                       data_root / "shaders/game_opaque_pass.frag")
{
}

void jkgm::shaders::game_pass_shader::set_screen_resolution(size<2, float> res)
{
    gl::set_uniform_vector(screen_resolution, res);
}

void jkgm::shaders::game_pass_shader::set_features(point<4, float> value)
{
    gl::set_uniform_vector(features, value);
}

void jkgm::shaders::game_pass_shader::set_blend_mode(int mode)
{
    gl::set_uniform_integer(blend_mode, mode);
}

void jkgm::shaders::game_pass_shader::set_fluid_thickness(float absorption)
{
    gl::set_uniform_float(fluid_thickness, absorption);
}

void jkgm::shaders::game_pass_shader::set_depth_clip(bool enabled)
{
    gl::set_uniform_integer(depth_clip, enabled ? 1 : 0);
}

void jkgm::shaders::game_pass_shader::set_flare_mode(bool enabled)
{
    gl::set_uniform_integer(flare_mode,enabled ? 1 : 0);
}

void jkgm::shaders::game_pass_shader::set_scene_depth_sampler(int sampler)
{
    gl::set_uniform_integer(scene_depth, sampler);
}

void jkgm::shaders::game_pass_shader::set_albedo_map_sampler(int sampler)
{
    gl::set_uniform_integer(albedo_map, sampler);
}

void jkgm::shaders::game_pass_shader::set_albedo_transition(
    int sampler, point<4, float> params)
{
    gl::set_uniform_integer(albedo_map2, sampler);
    gl::set_uniform_vector(albedo_transition, params);
}

void jkgm::shaders::game_pass_shader::set_albedo_factor(color factor)
{
    gl::set_uniform_vector(albedo_factor, factor);
}

void jkgm::shaders::game_pass_shader::set_normal_map_samplers(int first, int second)
{
    gl::set_uniform_integer(normal_map, first);
    gl::set_uniform_integer(normal_map2, second);
}

void jkgm::shaders::game_pass_shader::set_normal_params(point<4, float> params,
                                                         point<2, float> scroll1,
                                                         point<2, float> scroll2)
{
    gl::set_uniform_vector(normal_params, params);
    gl::set_uniform_vector(normal_uv_offset, scroll1);
    gl::set_uniform_vector(normal_uv_offset2, scroll2);
}

void jkgm::shaders::game_pass_shader::set_emissive_map_sampler(int sampler)
{
    gl::set_uniform_integer(emissive_map, sampler);
}

void jkgm::shaders::game_pass_shader::set_emissive_factor(color_rgb factor)
{
    gl::set_uniform_vector(emissive_factor, factor);
}

void jkgm::shaders::game_pass_shader::set_uv_offsets(point<2, float> albedo,
                                                     point<2, float> emissive)
{
    gl::set_uniform_vector(albedo_uv_offset, albedo);
    gl::set_uniform_vector(emissive_uv_offset, emissive);
}

void jkgm::shaders::game_pass_shader::set_alpha_cutoff(float cutoff)
{
    gl::set_uniform_float(alpha_cutoff, cutoff);
}

void jkgm::shaders::game_pass_shader::set_fog(point<4, float> params, color_rgb color)
{
    // If a shader does not declare these uniforms, the locations are -1 and OpenGL
    // silently ignores the calls, so stock shaders keep working.
    gl::set_uniform_vector(fog_params, params);
    gl::set_uniform_vector(fog_color, color);
}

void jkgm::shaders::game_pass_shader::set_displacement_map_sampler(int sampler)
{
    gl::set_uniform_integer(displacement_map, sampler);
}

void jkgm::shaders::game_pass_shader::set_surface_map_sampler(int sampler)
{
    gl::set_uniform_integer(surface_map, sampler);
}

void jkgm::shaders::game_pass_shader::set_env_map_sampler(int sampler)
{
    gl::set_uniform_integer(env_map, sampler);
}

void jkgm::shaders::game_pass_shader::set_env_transition(
    int sampler, point<4, float> params)
{
    gl::set_uniform_integer(env_map2, sampler);
    gl::set_uniform_vector(env_transition, params);
}

void jkgm::shaders::game_pass_shader::set_reflection(point<4, float> reflection,
                                                      point<4, float> env)
{
    // Shaders that do not declare these uniforms get location -1, which OpenGL ignores.
    gl::set_uniform_vector(reflection_params, reflection);
    gl::set_uniform_vector(env_params, env);
}

void jkgm::shaders::game_pass_shader::set_sun(
    point<4, float> direction, point<4, float> color_intensity)
{
    gl::set_uniform_vector(sun_direction, direction);
    gl::set_uniform_vector(sun_color_intensity, color_intensity);
}

void jkgm::shaders::game_pass_shader::set_camera(point<4, float> right,
                                                  point<4, float> forward,
                                                  point<4, float> up,
                                                  point<4, float> view)
{
    gl::set_uniform_vector(camera_right, right);
    gl::set_uniform_vector(camera_forward, forward);
    gl::set_uniform_vector(camera_up, up);
    gl::set_uniform_vector(view_params, view);
}

void jkgm::shaders::game_pass_shader::set_displacement_factor(float factor)
{
    gl::set_uniform_float(displacement_factor, factor);
}

jkgm::shaders::game_post_ssao_shader::game_post_ssao_shader(fs::path const &data_root)
    : opengl_shader("game_post_ssao",
                    data_root / "shaders/postprocess.vert",
                    data_root / "shaders/post_ssao.frag")
    , normal_image(gl::get_uniform_location(the_program, "normal_image"))
    , depth_image(gl::get_uniform_location(the_program, "depth_image"))
    , noise_image(gl::get_uniform_location(the_program, "noise_image"))
{
    samples.reserve(num_kernel_samples);

    std::string name_buf;
    for(int i = 0; i < num_kernel_samples; ++i) {
        name_buf = str(fmt("samples[", i, "]"));
        samples.push_back(gl::get_uniform_location(the_program, name_buf));
    }
}

void jkgm::shaders::game_post_ssao_shader::set_normal_sampler(int sampler)
{
    gl::set_uniform_integer(normal_image, sampler);
}

void jkgm::shaders::game_post_ssao_shader::set_depth_sampler(int sampler)
{
    gl::set_uniform_integer(depth_image, sampler);
}

void jkgm::shaders::game_post_ssao_shader::set_noise_sampler(int sampler)
{
    gl::set_uniform_integer(noise_image, sampler);
}

void jkgm::shaders::game_post_ssao_shader::set_samples(span<point<3, float> const> values)
{
    size_t i = 0;
    for(auto const &em : values) {
        gl::set_uniform_vector(samples.at(i++), em);
    }
}

jkgm::shaders::game_post_opaque_composite_shader::game_post_opaque_composite_shader(
    fs::path const &data_root)
    : opengl_shader("game_post_opaque_composite",
                    data_root / "shaders/postprocess.vert",
                    data_root / "shaders/post_opaque_composite.frag")
    , color_image(gl::get_uniform_location(the_program, "color_image"))
    , emissive_image(gl::get_uniform_location(the_program, "emissive_image"))
    , occlusion_image(gl::get_uniform_location(the_program, "occlusion_image"))
    , depth_image(gl::get_uniform_location(the_program, "depth_image"))
    , fog_params(gl::get_uniform_location(the_program, "fog_params"))
    , fog_color(gl::get_uniform_location(the_program, "fog_color"))
    , decal_active(gl::get_uniform_location(the_program, "decal_active"))
    , decal_image(gl::get_uniform_location(the_program, "decal_image"))
    , normal_image(gl::get_uniform_location(the_program, "normal_image"))
    , decal_origin(gl::get_uniform_location(the_program, "decal_origin"))
    , decal_right(gl::get_uniform_location(the_program, "decal_right"))
    , decal_up(gl::get_uniform_location(the_program, "decal_up"))
    , decal_forward(gl::get_uniform_location(the_program, "decal_forward"))
    , decal_options(gl::get_uniform_location(the_program, "decal_options"))
    , camera_origin(gl::get_uniform_location(the_program, "camera_origin"))
    , camera_right(gl::get_uniform_location(the_program, "camera_right"))
    , camera_forward(gl::get_uniform_location(the_program, "camera_forward"))
    , camera_up(gl::get_uniform_location(the_program, "camera_up"))
{
}

void jkgm::shaders::game_post_opaque_composite_shader::set_color_sampler(int sampler)
{
    gl::set_uniform_integer(color_image, sampler);
}

void jkgm::shaders::game_post_opaque_composite_shader::set_emissive_sampler(int sampler)
{
    gl::set_uniform_integer(emissive_image, sampler);
}

void jkgm::shaders::game_post_opaque_composite_shader::set_occlusion_sampler(int sampler)
{
    gl::set_uniform_integer(occlusion_image, sampler);
}

void jkgm::shaders::game_post_opaque_composite_shader::set_depth_sampler(int sampler)
{
    gl::set_uniform_integer(depth_image, sampler);
}

void jkgm::shaders::game_post_opaque_composite_shader::set_fog(point<4,float> params,
                                                                color_rgb fog)
{
    gl::set_uniform_vector(fog_params,params);
    gl::set_uniform_vector(fog_color,fog);
}

void jkgm::shaders::game_post_opaque_composite_shader::disable_projected_decal()
{
    gl::set_uniform_integer(decal_active,0);
}

void jkgm::shaders::game_post_opaque_composite_shader::set_projected_decal(
    point<4,float> origin,point<4,float> right,point<4,float> up,
    point<4,float> forward,point<4,float> options,point<4,float> cam_origin,
    point<4,float> cam_right,point<4,float> cam_forward,point<4,float> cam_up)
{
    gl::set_uniform_integer(decal_active,1);
    gl::set_uniform_integer(decal_image,4);
    gl::set_uniform_integer(normal_image,5);
    gl::set_uniform_vector(decal_origin,origin);
    gl::set_uniform_vector(decal_right,right);
    gl::set_uniform_vector(decal_up,up);
    gl::set_uniform_vector(decal_forward,forward);
    gl::set_uniform_vector(decal_options,options);
    gl::set_uniform_vector(camera_origin,cam_origin);
    gl::set_uniform_vector(camera_right,cam_right);
    gl::set_uniform_vector(camera_forward,cam_forward);
    gl::set_uniform_vector(camera_up,cam_up);
}

jkgm::shaders::game_transparency_pass_shader::game_transparency_pass_shader(
    fs::path const &data_root)
    : game_pass_shader("game_transparency_pass",
                       data_root / "shaders/game.vert",
                       data_root / "shaders/game_trns_pass.frag")
{
}

jkgm::shaders::post_box4_shader::post_box4_shader(fs::path const &data_root)
    : opengl_shader("post_box4",
                    data_root / "shaders/postprocess.vert",
                    data_root / "shaders/post_box4.frag")
    , fbuf_image(gl::get_uniform_location(the_program, "fbuf_image"))
    , normal_image(gl::get_uniform_location(the_program, "normal_image"))
    , depth_image(gl::get_uniform_location(the_program, "depth_image"))
{
}

void jkgm::shaders::post_box4_shader::set_fbuf_sampler(int sampler)
{
    gl::set_uniform_integer(fbuf_image, sampler);
}

void jkgm::shaders::post_box4_shader::set_normal_sampler(int sampler)
{
    gl::set_uniform_integer(normal_image, sampler);
}

void jkgm::shaders::post_box4_shader::set_depth_sampler(int sampler)
{
    gl::set_uniform_integer(depth_image, sampler);
}

jkgm::shaders::post_gauss3_shader::post_gauss3_shader(fs::path const &data_root)
    : post_gauss_shader("post_gauss3",
                        data_root / "shaders/postprocess.vert",
                        data_root / "shaders/post_gauss3.frag")
    , fbuf_image(gl::get_uniform_location(the_program, "fbuf_image"))
    , blur_direction(gl::get_uniform_location(the_program, "blur_direction"))
{
}

void jkgm::shaders::post_gauss3_shader::set_fbuf_sampler(int sampler)
{
    gl::set_uniform_integer(fbuf_image, sampler);
}

void jkgm::shaders::post_gauss3_shader::set_blur_direction(direction<2, int> dir)
{
    gl::set_uniform_vector(blur_direction, dir);
}

jkgm::shaders::post_gauss7_shader::post_gauss7_shader(fs::path const &data_root)
    : post_gauss_shader("post_gauss7",
                        data_root / "shaders/postprocess.vert",
                        data_root / "shaders/post_gauss7.frag")
    , fbuf_image(gl::get_uniform_location(the_program, "fbuf_image"))
    , blur_direction(gl::get_uniform_location(the_program, "blur_direction"))
{
}

void jkgm::shaders::post_gauss7_shader::set_fbuf_sampler(int sampler)
{
    gl::set_uniform_integer(fbuf_image, sampler);
}

void jkgm::shaders::post_gauss7_shader::set_blur_direction(direction<2, int> dir)
{
    gl::set_uniform_vector(blur_direction, dir);
}

jkgm::shaders::post_gauss9_shader::post_gauss9_shader(fs::path const &data_root)
    : post_gauss_shader("post_gauss9",
                        data_root / "shaders/postprocess.vert",
                        data_root / "shaders/post_gauss9.frag")
    , fbuf_image(gl::get_uniform_location(the_program, "fbuf_image"))
    , blur_direction(gl::get_uniform_location(the_program, "blur_direction"))
{
}

void jkgm::shaders::post_gauss9_shader::set_fbuf_sampler(int sampler)
{
    gl::set_uniform_integer(fbuf_image, sampler);
}

void jkgm::shaders::post_gauss9_shader::set_blur_direction(direction<2, int> dir)
{
    gl::set_uniform_vector(blur_direction, dir);
}

jkgm::shaders::post_gauss15_shader::post_gauss15_shader(fs::path const &data_root)
    : post_gauss_shader("post_gauss15",
                        data_root / "shaders/postprocess.vert",
                        data_root / "shaders/post_gauss15.frag")
    , fbuf_image(gl::get_uniform_location(the_program, "fbuf_image"))
    , blur_direction(gl::get_uniform_location(the_program, "blur_direction"))
{
}

void jkgm::shaders::post_gauss15_shader::set_fbuf_sampler(int sampler)
{
    gl::set_uniform_integer(fbuf_image, sampler);
}

void jkgm::shaders::post_gauss15_shader::set_blur_direction(direction<2, int> dir)
{
    gl::set_uniform_vector(blur_direction, dir);
}

jkgm::shaders::post_low_pass_shader::post_low_pass_shader(fs::path const &data_root)
    : opengl_shader("post_low_pass",
                    data_root / "shaders/postprocess.vert",
                    data_root / "shaders/post_low_pass.frag")
    , fbuf_image(gl::get_uniform_location(the_program, "fbuf_image"))
{
}

void jkgm::shaders::post_low_pass_shader::set_fbuf_sampler(int sampler)
{
    gl::set_uniform_integer(fbuf_image, sampler);
}

jkgm::shaders::post_scale_shader::post_scale_shader(fs::path const &data_root)
    : opengl_shader("post_scale",
                    data_root / "shaders/postprocess.vert",
                    data_root / "shaders/post_scale.frag")
    , fbuf_image(gl::get_uniform_location(the_program, "fbuf_image"))
{
}

void jkgm::shaders::post_scale_shader::set_fbuf_sampler(int sampler)
{
    gl::set_uniform_integer(fbuf_image, sampler);
}

jkgm::shaders::post_spatial_blur_shader::post_spatial_blur_shader(
    fs::path const &data_root)
    : opengl_shader("post_spatial_blur",
                    data_root / "shaders/postprocess.vert",
                    data_root / "shaders/post_spatial_blur.frag")
    , source_image(gl::get_uniform_location(the_program, "source_image"))
    , depth_image(gl::get_uniform_location(the_program, "depth_image"))
    , blur_axis(gl::get_uniform_location(the_program, "blur_axis"))
    , screen_size(gl::get_uniform_location(the_program, "screen_size"))
    , distance_params(gl::get_uniform_location(the_program, "distance_params"))
    , peripheral_params(gl::get_uniform_location(the_program, "peripheral_params"))
{
}

void jkgm::shaders::post_spatial_blur_shader::set_source_sampler(int sampler)
{
    gl::set_uniform_integer(source_image, sampler);
}

void jkgm::shaders::post_spatial_blur_shader::set_depth_sampler(int sampler)
{
    gl::set_uniform_integer(depth_image, sampler);
}

void jkgm::shaders::post_spatial_blur_shader::set_blur_axis(int axis)
{
    gl::set_uniform_integer(blur_axis, axis);
}

void jkgm::shaders::post_spatial_blur_shader::set_screen_size(size<2, float> dims)
{
    gl::set_uniform_vector(screen_size, dims);
}

void jkgm::shaders::post_spatial_blur_shader::set_distance_params(point<4, float> params)
{
    gl::set_uniform_vector(distance_params, params);
}

void jkgm::shaders::post_spatial_blur_shader::set_peripheral_params(point<4, float> params)
{
    gl::set_uniform_vector(peripheral_params, params);
}

jkgm::shaders::post_frame_blend_shader::post_frame_blend_shader(
    fs::path const &data_root)
    : opengl_shader("post_frame_blend",
                    data_root / "shaders/postprocess.vert",
                    data_root / "shaders/post_frame_blend.frag")
    , current_image(gl::get_uniform_location(the_program, "current_image"))
    , history_image(gl::get_uniform_location(the_program, "history_image"))
    , blend_strength(gl::get_uniform_location(the_program, "blend_strength"))
    , retro_palette_sampler(gl::get_uniform_location(the_program, "retro_palette_lut"))
    , retro_palette_enabled(gl::get_uniform_location(the_program, "retro_palette_enabled"))
{
}

void jkgm::shaders::post_frame_blend_shader::set_current_sampler(int sampler)
{
    gl::set_uniform_integer(current_image, sampler);
}

void jkgm::shaders::post_frame_blend_shader::set_history_sampler(int sampler)
{
    gl::set_uniform_integer(history_image, sampler);
}

void jkgm::shaders::post_frame_blend_shader::set_retro_palette(int sampler,bool enabled)
{
    gl::set_uniform_integer(retro_palette_sampler,sampler);
    gl::set_uniform_integer(retro_palette_enabled,enabled?1:0);
}

void jkgm::shaders::post_frame_blend_shader::set_blend_strength(float strength)
{
    gl::set_uniform_float(blend_strength, strength);
}

jkgm::shaders::post_to_srgb_shader::post_to_srgb_shader(fs::path const &data_root)
    : opengl_shader("post_to_srgb",
                    data_root / "shaders/postprocess.vert",
                    data_root / "shaders/post_to_srgb.frag")
    , fbuf_image(gl::get_uniform_location(the_program, "fbuf_image"))
    , grain_params(gl::get_uniform_location(the_program, "grain_params"))
    , vignette_params(gl::get_uniform_location(the_program, "vignette_params"))
    , brightness(gl::get_uniform_location(the_program, "brightness"))
    , contrast(gl::get_uniform_location(the_program, "contrast"))
    , saturation(gl::get_uniform_location(the_program, "saturation"))
{
    bloom_fbuf.reserve(num_bloom_layers);
    bloom_weight.reserve(num_bloom_layers);

    std::string name_buf;
    for(int i = 0; i < num_bloom_layers; ++i) {
        name_buf = str(fmt("bloom_fbuf[", i, "]"));
        bloom_fbuf.push_back(gl::get_uniform_location(the_program, name_buf));

        name_buf = str(fmt("bloom_weight[", i, "]"));
        bloom_weight.push_back(gl::get_uniform_location(the_program, name_buf));
    }
}

void jkgm::shaders::post_to_srgb_shader::set_fbuf_sampler(int sampler)
{
    gl::set_uniform_integer(fbuf_image, sampler);
}

void jkgm::shaders::post_to_srgb_shader::set_bloom_samplers(span<int const> samplers)
{
    size_t i = 0;
    for(auto const &em : samplers) {
        gl::set_uniform_integer(bloom_fbuf.at(i++), em);
    }
}

void jkgm::shaders::post_to_srgb_shader::set_bloom_weights(span<float const> weights)
{
    size_t i = 0;
    for(auto const &em : weights) {
        gl::set_uniform_float(bloom_weight.at(i++), em);
    }
}

void jkgm::shaders::post_to_srgb_shader::set_film_grain(point<4, float> params)
{
    gl::set_uniform_vector(grain_params, params);
}

void jkgm::shaders::post_to_srgb_shader::set_vignette(point<4, float> params)
{
    gl::set_uniform_vector(vignette_params, params);
}

void jkgm::shaders::post_to_srgb_shader::set_color_filter(float brightness_value,
                                                          float contrast_value,
                                                          float saturation_value)
{
    gl::set_uniform_float(brightness, brightness_value);
    gl::set_uniform_float(contrast, contrast_value);
    gl::set_uniform_float(saturation, saturation_value);
}

