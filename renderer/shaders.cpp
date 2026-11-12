#include "shaders.hpp"
#include "texture_units.hpp"
#include "base/file_stream.hpp"
#include "base/log.hpp"
#include "base/memory_block.hpp"
#include "common/error_reporter.hpp"
#include "glad/gl.h"
#include <algorithm>
#include <cmath>
#include <sstream>

namespace jkgm {

jkgm::shaders::translucent_sun_uniforms::translucent_sun_uniforms(
    gl::program_view program)
    : enabled(gl::get_uniform_location(program,"translucent_sun_enabled"))
    , maps_ready(gl::get_uniform_location(program,"translucent_sun_maps_ready"))
    , dynamic_count(gl::get_uniform_location(program,"translucent_sun_dynamic_count"))
    , direction(gl::get_uniform_location(program,"translucent_sun_direction"))
    , color(gl::get_uniform_location(program,"translucent_sun_color"))
    , split(gl::get_uniform_location(program,"translucent_sun_split"))
    , bias_scale(gl::get_uniform_location(program,"translucent_sun_bias_scale"))
    , pcf_radius(gl::get_uniform_location(program,"translucent_sun_pcf_radius"))
    , dynamic_pcf_radius(gl::get_uniform_location(program,
        "translucent_sun_dynamic_pcf_radius"))
    , sky_aperture_active(gl::get_uniform_location(
        program,"translucent_sun_sky_aperture_active"))
    , sky_aperture_map(gl::get_uniform_location(
        program,"translucent_sun_sky_aperture_map"))
    , sky_aperture_depth_transform(gl::get_uniform_location(
        program,"translucent_sun_sky_aperture_depth_transform"))
    , near_depth(gl::get_uniform_location(program,"translucent_sun_near_depth"))
    , far_depth(gl::get_uniform_location(program,"translucent_sun_far_depth"))
    , dynamic_depth(gl::get_uniform_location(program,"translucent_sun_dynamic_depth"))
    , max_distance(gl::get_uniform_location(program,"translucent_sun_max_distance"))
{
    for(int i=0;i<10;++i) {
        auto const slot=std::to_string(i)+"]";
        x.push_back(gl::get_uniform_location(program,"translucent_sun_map_x["+slot));
        y.push_back(gl::get_uniform_location(program,"translucent_sun_map_y["+slot));
        z.push_back(gl::get_uniform_location(program,"translucent_sun_map_z["+slot));
        if(i<8) {
            rect.push_back(gl::get_uniform_location(
                program,"translucent_sun_tile_rect["+slot));
        }
    }
}

void jkgm::shaders::translucent_sun_uniforms::set_state(
    bool active,bool ready,int dynamic,point<3,float> sun_dir,
    point<4,float> sun_color,float split_distance,float receiver_bias,
    float max_range)
{
    gl::set_uniform_integer(enabled,active?1:0);
    gl::set_uniform_integer(maps_ready,ready?1:0);
    gl::set_uniform_integer(dynamic_count,dynamic);
    if(!active)return;
    gl::set_uniform_vector(direction,sun_dir);
    gl::set_uniform_vector(color,sun_color);
    gl::set_uniform_float(split,split_distance);
    gl::set_uniform_float(bias_scale,receiver_bias);
    gl::set_uniform_float(max_distance,max_range);
    if(ready) {
        gl::set_uniform_integer(near_depth,16);
        gl::set_uniform_integer(far_depth,17);
    }
    if(dynamic>0)gl::set_uniform_integer(dynamic_depth,18);
}

void jkgm::shaders::translucent_sun_uniforms::set_pcf_radius(
    float near_radius,float far_radius)
{
    gl::set_uniform_vector(pcf_radius,make_point(near_radius,far_radius));
}

void jkgm::shaders::translucent_sun_uniforms::set_dynamic_pcf_radius(
    float radius)
{
    gl::set_uniform_float(dynamic_pcf_radius,radius);
}

void jkgm::shaders::translucent_sun_uniforms::set_sky_aperture(
    bool active,int sampler,point<4,float> near_far_depth_transform)
{
    // The sampler must have its own unit even when the feature is off.
    gl::set_uniform_integer(sky_aperture_map,sampler);
    gl::set_uniform_integer(sky_aperture_active,active?1:0);
    if(!active)return;
    gl::set_uniform_vector(sky_aperture_depth_transform,
                           near_far_depth_transform);
}

void jkgm::shaders::translucent_sun_uniforms::set_map(
    int slot,point<4,float> row_x,point<4,float> row_y,
    point<4,float> row_z,point<4,float> tile_rect)
{
    if(slot<0 || slot>=10)return;
    gl::set_uniform_vector(x[slot],row_x);
    gl::set_uniform_vector(y[slot],row_y);
    gl::set_uniform_vector(z[slot],row_z);
    if(slot>=2) {
        gl::set_uniform_vector(rect[slot-2],tile_rect);
    }
}

    namespace {
        // GLSL 3.3 has no portable #include. Expand local quoted includes
        // before submitting source to GL, keeping formulas as editable GLSL.
        // Only same-directory names are accepted; avoid cyclic includes.
        std::string load_shader_source(fs::path const &filename,
                                       unsigned depth=0)
        {
            if(depth>8U) {
                report_fatal_message(str(fmt(
                    "JkGfxMod shader include nesting is too deep: ",
                    filename.generic_string())));
            }
            memory_block mb;
            try {
                auto input=make_file_input_stream(filename);
                memory_output_block mob(&mb);
                input->copy_to(&mob);
            }
            catch(std::exception const &e) {
                report_fatal_message(str(fmt(
                    "JkGfxMod could not open an essential shader file: ",
                    filename.generic_string(),"\n",e.what())));
            }
            // memory_block::str() is a string_view; MSVC's stream
            // constructor requires a concrete std::string.
            std::istringstream lines{std::string(mb.str())};
            std::string line,output;
            while(std::getline(lines,line)) {
                auto const p=line.find_first_not_of(" \t");
                if(p!=std::string::npos &&
                   line.compare(p,8,"#include")==0 &&
                   (p+8==line.size() || line[p+8]==' ' ||
                    line[p+8]=='\t')) {
                    auto const first=line.find('"',p+8);
                    auto const last=first==std::string::npos
                        ? std::string::npos : line.find('"',first+1);
                    if(first==std::string::npos ||
                       last==std::string::npos || last==first+1) {
                        report_fatal_message(str(fmt(
                            "Malformed shader #include in ",
                            filename.generic_string(),": ",line)));
                    }
                    auto const name=line.substr(first+1,last-first-1);
                    if(name=="." || name==".." ||
                       name.find('/')!=std::string::npos ||
                       name.find('\\')!=std::string::npos ||
                       name.find(':')!=std::string::npos) {
                        report_fatal_message(str(fmt(
                            "Only local shader includes are permitted in ",
                            filename.generic_string(),": ",name)));
                    }
                    output+=load_shader_source(filename.parent_path()/name,
                                               depth+1U);
                } else {
                    output+=line;
                    output+='\n';
                }
            }
            return output;
        }

        gl::shader compile_shader_from_file(fs::path const &filename,
                                            gl::shader_type type)
        {
            diagnostic_context dc(filename.generic_string());
            gl::shader rv(type);
            auto source=load_shader_source(filename);
            // The optional sparse voxel vertex path requires two distinct
            // GL_TEXTURE_BUFFER units AFTER sector=19 and sky=20. Compile it
            // out entirely on low-end GL 3.3 devices, so inactive samplers
            // cannot alias sampler2D bindings at unit 0.
            if(type==gl::shader_type::vertex &&
               filename.filename()=="gpu_3do_preview.vert") {
                GLint fragment_units=0,vertex_units=0,combined_units=0;
                glGetIntegerv(GL_MAX_TEXTURE_IMAGE_UNITS,&fragment_units);
                glGetIntegerv(GL_MAX_VERTEX_TEXTURE_IMAGE_UNITS,&vertex_units);
                glGetIntegerv(GL_MAX_COMBINED_TEXTURE_IMAGE_UNITS,&combined_units);
                if(fragment_units>=23 && vertex_units>=2 &&
                   combined_units>=23) {
                    auto const newline=source.find('\n');
                    if(source.compare(0,8,"#version")!=0 ||
                       newline==std::string::npos)
                        report_fatal_message("Continuous voxel vertex GLSL requires #version");
                    source.insert(newline+1U,
                                  "#define JKGM_CONTINUOUS_AMBIENT_GPU 1\n");
                }
            }
            // Optional array samplers require a unique forward unit (20).
            // When the GPU exposes fewer than 21 fragment units, remove the
            // sampler at compile-time, not merely via a runtime uniform.
            if(type==gl::shader_type::fragment) {
                GLint units=0;
                glGetIntegerv(GL_MAX_TEXTURE_IMAGE_UNITS,&units);
                if(units>=texture_units::forward_next_extended_required_units) {
                    auto const newline=source.find('\n');
                    if(source.compare(0,8,"#version")!=0 ||
                       newline==std::string::npos)
                        report_fatal_message(
                            "Sky aperture shader requires a #version directive");
                    source.insert(newline+1U,
                                  "#define JKGM_SUN_SKY_APERTURE 1\n");
                }
            }
            gl::shader_source(rv,make_string_span(""),make_span(source));
            gl::compile_shader(rv);
            if(!gl::get_shader_compile_status(rv)) {
                auto err_str=gl::get_shader_info_log(rv);
                report_fatal_message(str(fmt(
                    "JkGfxMod failed to compile an essential shader. This may have "
                    "happened because your graphics device does not support "
                    "OpenGL 3.3, because a shader file has been incorrectly "
                    "modified, or because of a JkGfxMod bug.\n\nDetails: Failed "
                    "to compile ",filename.generic_string(),"\n",err_str)));
            }
            return rv;
        }

        // A newly linked program initializes ALL sampler uniforms to unit 0.
        // Disabled features may keep sampler2DShadow/samplerBuffer uniforms
        // active, so assign the registry before the very first draw, not
        // only when a shadow or material branch becomes active.
        void initialize_registered_samplers(std::string const &name,
                                            gl::program_view program)
        {
            auto bind=[&](auto const &entries) {
                GLint max_fragment_units=0;
                glGetIntegerv(GL_MAX_TEXTURE_IMAGE_UNITS,&max_fragment_units);
                GLint previous=0;
                glGetIntegerv(GL_CURRENT_PROGRAM,&previous);
                // Use raw GL deliberately: gl::use_program tracks a cached
                // current program for uniform-call deduplication. Restore
                // the previous real GL program before returning.
                glUseProgram(*program);
                for(auto const &entry:entries) {
                    GLint const location=glGetUniformLocation(
                        *program,entry.uniform_name);
                    if(location<0)continue; // optimized out on this variant
                    if(entry.unit>=max_fragment_units) {
                        LOG_WARNING("GL sampler unit unavailable in ",name,
                                    ": ",entry.uniform_name," unit=",entry.unit,
                                    " fragment_units=",max_fragment_units);
                        continue;
                    }
                    glUniform1i(location,entry.unit);
                }
                glUseProgram(static_cast<GLuint>(previous));
            };
            if(name=="gpu_3do_preview")bind(texture_units::gpu_3do_preview);
            else if(name=="world_cache_preview")bind(texture_units::world_cache);
            else if(name=="game_opaque_pass")bind(texture_units::game_opaque_pass);
            else if(name=="game_transparency_pass")bind(texture_units::game_trns_pass);
            else if(name=="game_post_opaque_composite")
                bind(texture_units::post_opaque_composite);
            else if(name=="game_post_opaque_half_sun")
                bind(texture_units::post_opaque_half_sun);
        }

        void link_program_from_files(std::string const &name,
                                     gl::program_view prog,
                                     fs::path const &vx,
                                     fs::path const &fg)
        {
            auto vx_shader = compile_shader_from_file(vx,gl::shader_type::vertex);
            auto fg_shader = compile_shader_from_file(
                fg,gl::shader_type::fragment);

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
            initialize_registered_samplers(name,prog);
        }
    }
}

jkgm::opengl_shader::opengl_shader(std::string const &name,
                                   fs::path const &vertex_part,
                                   fs::path const &fragment_part)
{
    link_program_from_files(
        name,the_program,vertex_part,fragment_part);
}

void jkgm::opengl_shader::use_program()
{
    gl::use_program(the_program);
}

jkgm::shaders::menu_shader::menu_shader(fs::path const &data_root, bool fullscreen)
    : opengl_shader(fullscreen ? "menu_present" : "menu",
                    data_root / (fullscreen ? "shaders/postprocess.vert" : "shaders/game.vert"),
                    data_root / "shaders/menu.frag")
    , tex(gl::get_uniform_location(the_program, "tex"))
{
}

void jkgm::shaders::menu_shader::set_menu_sampler(int sampler)
{
    gl::set_uniform_integer(tex, sampler);
}

jkgm::shaders::world_cache_preview_shader::world_cache_preview_shader(
    fs::path const &root)
    : opengl_shader("world_cache_preview",
                    root/"shaders/world_cache.vert",
                    root/"shaders/world_cache.frag")
    , view_right(gl::get_uniform_location(the_program,"view_right"))
    , view_forward(gl::get_uniform_location(the_program,"view_forward"))
    , view_up(gl::get_uniform_location(the_program,"view_up"))
    , view_origin(gl::get_uniform_location(the_program,"view_origin"))
    , projection(gl::get_uniform_location(the_program,"projection"))
    , albedo_sampler(gl::get_uniform_location(the_program,"albedo_map"))
    , uv_scale(gl::get_uniform_location(the_program,"uv_scale"))
    , uv_scroll(gl::get_uniform_location(the_program,"uv_scroll"))
    , material_tint(gl::get_uniform_location(the_program,"material_tint"))
    , solid_color(gl::get_uniform_location(the_program,"solid_color"))
    , albedo2_sampler(gl::get_uniform_location(the_program,"albedo_map2"))
    , albedo_source(gl::get_uniform_location(the_program,"albedo_source"))
    , albedo_source2(gl::get_uniform_location(the_program,"albedo_source2"))
    , albedo_transition(gl::get_uniform_location(the_program,"albedo_transition"))
    , emissive_sampler(gl::get_uniform_location(the_program,"emissive_map"))
    , emissive_factor(gl::get_uniform_location(the_program,"emissive_factor"))
    , emissive_scroll(gl::get_uniform_location(the_program,"emissive_scroll"))
    , has_emissive(gl::get_uniform_location(the_program,"has_emissive"))
    , emissive_only(gl::get_uniform_location(the_program,"emissive_only"))
    , solid_fullbright(gl::get_uniform_location(the_program,"solid_fullbright"))
    , palette_fullbright_sampler(gl::get_uniform_location(the_program,"palette_fullbright_mask"))
    , has_palette_fullbright(gl::get_uniform_location(the_program,"has_palette_fullbright"))
    , reflection_surface_sampler(gl::get_uniform_location(the_program,"surface_map"))
    , reflection_normal_sampler(gl::get_uniform_location(the_program,"normal_map"))
    , reflection_normal2_sampler(gl::get_uniform_location(the_program,"normal_map2"))
    , reflection_env_sampler(gl::get_uniform_location(the_program,"env_map"))
    , reflection_env2_sampler(gl::get_uniform_location(the_program,"env_map2"))
    , reflection_params(gl::get_uniform_location(the_program,"specular_params"))
    , reflection_normal_params(gl::get_uniform_location(the_program,"normal_specular_params"))
    , normal_mapped_diffuse(gl::get_uniform_location(the_program,"normal_mapped_diffuse"))
    , specular_normal_mode(gl::get_uniform_location(the_program,"experimental_specular_normal_mode"))
    , reflection_scroll(gl::get_uniform_location(the_program,"normal_specular_scroll"))
    , reflection_scroll2(gl::get_uniform_location(the_program,"normal_specular_scroll2"))
    , environment_info(gl::get_uniform_location(the_program,"environment_info"))
    , camera_world_origin(gl::get_uniform_location(the_program,"camera_world_origin"))
    , camera_right_axis(gl::get_uniform_location(the_program,"camera_right_axis"))
    , camera_forward_axis(gl::get_uniform_location(the_program,"camera_forward_axis"))
    , camera_up_axis(gl::get_uniform_location(the_program,"camera_up_axis"))
    , planar_sampler(gl::get_uniform_location(the_program,"planar_reflection_map"))
    , planar_ready(gl::get_uniform_location(the_program,"planar_reflection_ready"))
    , planar_plane(gl::get_uniform_location(the_program,"planar_receiver_plane"))
    , planar_camera_origin(gl::get_uniform_location(the_program,"planar_camera_origin"))
    , planar_camera_right(gl::get_uniform_location(the_program,"planar_camera_right"))
    , planar_camera_forward(gl::get_uniform_location(the_program,"planar_camera_forward"))
    , planar_camera_up(gl::get_uniform_location(the_program,"planar_camera_up"))
    , planar_tan_half_fov(gl::get_uniform_location(the_program,"planar_tan_half_fov"))
    , alpha_cutout(gl::get_uniform_location(the_program,"alpha_cutout"))
    , alpha_cutoff(gl::get_uniform_location(the_program,"alpha_cutoff"))
    , transparent_pass(gl::get_uniform_location(the_program,"transparent_pass"))
    , alpha_mode(gl::get_uniform_location(the_program,"alpha_mode"))
    , face_opacity(gl::get_uniform_location(the_program,"face_opacity"))
    , blend_mode(gl::get_uniform_location(the_program,"blend_mode"))
    , scene_depth_sampler(gl::get_uniform_location(the_program,"scene_depth"))
    , fluid_thickness(gl::get_uniform_location(the_program,"fluid_thickness"))
    , fluid_debug_mode(gl::get_uniform_location(the_program,"fluid_debug_mode"))
    , fog_params(gl::get_uniform_location(the_program,"fog_params"))
    , fog_color(gl::get_uniform_location(the_program,"fog_color"))
    , sky_mode(gl::get_uniform_location(the_program,"sky_mode"))
    , sky_horizon(gl::get_uniform_location(the_program,"sky_horizon"))
    , sky_angles(gl::get_uniform_location(the_program,"sky_angles"))
    , sky_camera_origin(gl::get_uniform_location(the_program,"sky_camera_origin"))
    , sky_camera_right(gl::get_uniform_location(the_program,"sky_camera_right"))
    , sky_camera_forward(gl::get_uniform_location(the_program,"sky_camera_forward"))
    , sky_camera_up(gl::get_uniform_location(the_program,"sky_camera_up"))
    , sky_ceiling_height(gl::get_uniform_location(the_program,"sky_ceiling_height"))
    , sky_offset(gl::get_uniform_location(the_program,"sky_offset"))
    , sky_face_offset(gl::get_uniform_location(the_program,"sky_face_offset"))
    , sky_viewport(gl::get_uniform_location(the_program,"sky_viewport"))
    , num_lights(gl::get_uniform_location(the_program,"num_lights"))
    , light_attenuation(gl::get_uniform_location(the_program,"light_attenuation"))
    , jk_light_limit(gl::get_uniform_location(the_program,"jk_light_limit"))
    , world_pixel_dynamic_lighting(gl::get_uniform_location(
          the_program,"world_pixel_dynamic_lighting"))
    , world_dynamic_light_specular(gl::get_uniform_location(
          the_program,"world_dynamic_light_specular"))
    , jk_color_filter(gl::get_uniform_location(the_program,"jk_color_filter"))
    , jk_color_tint(gl::get_uniform_location(the_program,"jk_color_tint"))
    , jk_color_add_fade(gl::get_uniform_location(the_program,"jk_color_add_fade"))
    , translucent_sun(the_program)
{
    for(unsigned i=0;i<16U;++i) {
        light_position_radius.push_back(gl::get_uniform_location(
            the_program,"light_position_radius["+std::to_string(i)+"]"));
        light_intensity.push_back(gl::get_uniform_location(
            the_program,"light_intensity["+std::to_string(i)+"]"));
        light_color.push_back(gl::get_uniform_location(
            the_program,"light_color["+std::to_string(i)+"]"));
    }
}
void jkgm::shaders::world_cache_preview_shader::set_view_matrix(
    point<4,float> r,point<4,float> f,point<4,float> u,point<4,float> t)
{
    gl::set_uniform_vector(view_right,r);
    gl::set_uniform_vector(view_forward,f);
    gl::set_uniform_vector(view_up,u);
    gl::set_uniform_vector(view_origin,t);
}
void jkgm::shaders::world_cache_preview_shader::set_projection(
    point<4,float> p)
{
    gl::set_uniform_vector(projection,p);
}
void jkgm::shaders::world_cache_preview_shader::set_texture(
    float width,float height,point<4,float> tint,point<2,float> scroll,
    int source)
{
    gl::set_uniform_integer(solid_color,0);
    gl::set_uniform_integer(albedo_source,source);
    gl::set_uniform_integer(alpha_cutout,0);
    gl::set_uniform_integer(albedo_sampler,0);
    gl::set_uniform_vector(uv_scale,make_point(1.0f/width,1.0f/height));
    gl::set_uniform_vector(uv_scroll,scroll);
    gl::set_uniform_vector(material_tint,tint);
}
void jkgm::shaders::world_cache_preview_shader::set_solid_color(
    point<4,float> tint)
{
    gl::set_uniform_integer(solid_color,1);
    gl::set_uniform_integer(albedo_source,2);
    gl::set_uniform_integer(alpha_cutout,0);
    gl::set_uniform_vector(material_tint,tint);
}
void jkgm::shaders::world_cache_preview_shader::set_solid_fullbright(bool enabled)
{
    gl::set_uniform_integer(solid_fullbright,enabled?1:0);
}
void jkgm::shaders::world_cache_preview_shader::set_palette_fullbright(bool enabled)
{
    gl::set_uniform_integer(palette_fullbright_sampler,11);
    gl::set_uniform_integer(has_palette_fullbright,enabled?1:0);
}
void jkgm::shaders::world_cache_preview_shader::set_albedo_transition(
    int source2,float weight)
{
    gl::set_uniform_integer(albedo2_sampler,6);
    gl::set_uniform_integer(albedo_source2,source2);
    gl::set_uniform_float(albedo_transition,weight);
}
void jkgm::shaders::world_cache_preview_shader::set_emissive(
    bool textured,point<4,float> intensity,point<2,float> scroll)
{
    gl::set_uniform_integer(emissive_sampler,1);
    gl::set_uniform_integer(has_emissive,textured?1:0);
    gl::set_uniform_vector(emissive_factor,
        make_point(get<0>(intensity),get<1>(intensity),
                   get<2>(intensity)));
    gl::set_uniform_vector(emissive_scroll,scroll);
}
void jkgm::shaders::world_cache_preview_shader::set_normal_mapped_diffuse(
    bool enabled)
{
    gl::set_uniform_integer(normal_mapped_diffuse,enabled?1:0);
}
void jkgm::shaders::world_cache_preview_shader::set_reflection(
    point<4,float> material,point<4,float> normals,
    point<2,float> scroll,point<2,float> scroll2,
    bool from,bool to,float weight,int mode)
{
    gl::set_uniform_integer(reflection_surface_sampler,2);
    gl::set_uniform_integer(reflection_normal_sampler,3);
    gl::set_uniform_integer(reflection_normal2_sampler,5);
    gl::set_uniform_integer(reflection_env_sampler,4);
    gl::set_uniform_integer(reflection_env2_sampler,7);
    gl::set_uniform_vector(reflection_params,material);
    gl::set_uniform_vector(reflection_normal_params,normals);
    gl::set_uniform_vector(reflection_scroll,scroll);
    gl::set_uniform_vector(reflection_scroll2,scroll2);
    gl::set_uniform_vector(environment_info,
        make_point(from?1.0f:0.0f,static_cast<float>(mode),
                   to?1.0f:0.0f,weight));
}
void jkgm::shaders::world_cache_preview_shader::set_reflection_camera(
    std::array<float,3> const &position,
    std::array<float,3> const &right,
    std::array<float,3> const &forward,
    std::array<float,3> const &up)
{
    gl::set_uniform_vector(camera_world_origin,
                           make_point(position[0],position[1],position[2]));
    gl::set_uniform_vector(camera_right_axis,
                           make_point(right[0],right[1],right[2]));
    gl::set_uniform_vector(camera_forward_axis,
                           make_point(forward[0],forward[1],forward[2]));
    gl::set_uniform_vector(camera_up_axis,
                           make_point(up[0],up[1],up[2]));
}
void jkgm::shaders::world_cache_preview_shader::set_planar_reflection(
    bool ready,point<4,float> plane,point<3,float> origin,
    point<3,float> right,point<3,float> forward,point<3,float> up,
    point<2,float> tan_half_fov)
{
    // Unit 15 is within the OpenGL 3.3 minimum fragment sampler count
    // and is reserved for the world-only planar source in this pass.
    gl::set_uniform_integer(planar_sampler,15);
    gl::set_uniform_integer(planar_ready,ready?1:0);
    if(!ready)return;
    gl::set_uniform_vector(planar_plane,plane);
    gl::set_uniform_vector(planar_camera_origin,origin);
    gl::set_uniform_vector(planar_camera_right,right);
    gl::set_uniform_vector(planar_camera_forward,forward);
    gl::set_uniform_vector(planar_camera_up,up);
    gl::set_uniform_vector(planar_tan_half_fov,tan_half_fov);
}

void jkgm::shaders::world_cache_preview_shader::set_alpha_cutout(
    float cutoff)
{
    gl::set_uniform_integer(alpha_cutout,1);
    gl::set_uniform_float(alpha_cutoff,cutoff);
}

void jkgm::shaders::world_cache_preview_shader::set_scene_depth(int sampler)
{
    gl::set_uniform_integer(scene_depth_sampler,sampler);
}
void jkgm::shaders::world_cache_preview_shader::set_fluid_thickness(
    float thickness)
{
    gl::set_uniform_float(fluid_thickness,thickness);
}
void jkgm::shaders::world_cache_preview_shader::set_fluid_debug_mode(int mode)
{
    gl::set_uniform_integer(fluid_debug_mode,mode);
}
void jkgm::shaders::world_cache_preview_shader::set_alpha_policy(
    bool transparent,int mode,float cutoff,float native_opacity,int blend,
    bool emission_only)
{
    gl::set_uniform_integer(emissive_only,emission_only?1:0);
    gl::set_uniform_integer(blend_mode,blend);
    gl::set_uniform_integer(transparent_pass,transparent?1:0);
    gl::set_uniform_integer(alpha_mode,mode);
    gl::set_uniform_float(alpha_cutoff,cutoff);
    gl::set_uniform_float(face_opacity,native_opacity);
    gl::set_uniform_integer(alpha_cutout,0);
}
void jkgm::shaders::world_cache_preview_shader::set_fog(
    point<4,float> params,point<3,float> color)
{
    gl::set_uniform_vector(fog_params,params);
    gl::set_uniform_vector(fog_color,color);
}

void jkgm::shaders::world_cache_preview_shader::set_sky(
    int mode,point<4,float> horizon,point<3,float> angles,
    point<3,float> camera_origin,point<3,float> right,
    point<3,float> forward,point<3,float> up,
    float ceiling_height,point<2,float> offset,
    point<2,float> face_offset,point<2,float> viewport)
{
    gl::set_uniform_integer(sky_mode,mode);
    if(mode==0)return;
    gl::set_uniform_vector(sky_horizon,horizon);
    gl::set_uniform_vector(sky_angles,angles);
    gl::set_uniform_vector(sky_camera_origin,camera_origin);
    gl::set_uniform_vector(sky_camera_right,right);
    gl::set_uniform_vector(sky_camera_forward,forward);
    gl::set_uniform_vector(sky_camera_up,up);
    gl::set_uniform_float(sky_ceiling_height,ceiling_height);
    gl::set_uniform_vector(sky_offset,offset);
    gl::set_uniform_vector(sky_face_offset,face_offset);
    gl::set_uniform_vector(sky_viewport,viewport);
}

void jkgm::shaders::world_cache_preview_shader::set_jk_color_effects(
    point<4,float> filter,point<4,float> tint,point<4,float> add_fade)
{
    gl::set_uniform_vector(jk_color_filter,filter);
    gl::set_uniform_vector(jk_color_tint,tint);
    gl::set_uniform_vector(jk_color_add_fade,add_fade);
}
void jkgm::shaders::world_cache_preview_shader::set_specular_normal_mode(
    int mode)
{
    gl::set_uniform_integer(specular_normal_mode,mode);
}

void jkgm::shaders::world_cache_preview_shader::set_specular_lighting(
    bool pixel_diffuse,bool point_specular)
{
    gl::set_uniform_integer(world_pixel_dynamic_lighting,pixel_diffuse?1:0);
    gl::set_uniform_integer(world_dynamic_light_specular,point_specular?1:0);
}
void jkgm::shaders::world_cache_preview_shader::set_light_brightness_limit(
    float limit)
{
    gl::set_uniform_float(jk_light_limit,limit);
}
void jkgm::shaders::world_cache_preview_shader::set_lights(
    std::vector<world_cache_point_light> const &lights,float attenuation,
    std::array<std::array<float,3>,64> const &saber_rgb,
    std::array<bool,64> const &saber_valid)
{
    gl::set_uniform_integer(num_lights,static_cast<int>(lights.size()));
    gl::set_uniform_float(light_attenuation,attenuation);
    for(size_t i=0;i<lights.size();++i) {
        auto const &light=lights[i];
        gl::set_uniform_vector(light_position_radius[i],
            make_point(light.position[0],light.position[1],
                       light.position[2],light.radius));
        gl::set_uniform_float(light_intensity[i],light.intensity);
        unsigned const id=light.native_id;
        auto const rgb=id<64U && saber_valid[id]
            ?saber_rgb[id]:std::array<float,3>{{1.f,1.f,1.f}};
        gl::set_uniform_vector(light_color[i],
            make_point(rgb[0],rgb[1],rgb[2]));
    }
}

jkgm::shaders::game_pass_shader::game_pass_shader(std::string const &name,
                                                  fs::path const &vertex_part,
                                                  fs::path const &fragment_part)
    : opengl_shader(name,vertex_part,fragment_part)
    , screen_resolution(gl::get_uniform_location(the_program, "screen_resolution"))
    , features(gl::get_uniform_location(the_program, "features"))
    , blend_mode(gl::get_uniform_location(the_program, "blend_mode"))
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
    , specular_normal_mode(gl::get_uniform_location(the_program, "experimental_specular_normal_mode"))
    , camera_right(gl::get_uniform_location(the_program, "camera_right"))
    , camera_forward(gl::get_uniform_location(the_program, "camera_forward"))
    , camera_up(gl::get_uniform_location(the_program, "camera_up"))
    , view_params(gl::get_uniform_location(the_program, "view_params"))
    , translucent_spotlight_count(gl::get_uniform_location(the_program,"translucent_spotlight_count"))
    , translucent_spotlight_resolution(gl::get_uniform_location(the_program,"translucent_spotlight_resolution"))
    , translucent_spotlight_camera_origin(gl::get_uniform_location(the_program,"translucent_spotlight_camera_origin"))
    , translucent_spotlight_camera_right(gl::get_uniform_location(the_program,"translucent_spotlight_camera_right"))
    , translucent_spotlight_camera_forward(gl::get_uniform_location(the_program,"translucent_spotlight_camera_forward"))
    , translucent_spotlight_camera_up(gl::get_uniform_location(the_program,"translucent_spotlight_camera_up"))
    , translucent_shadow_slot(gl::get_uniform_location(the_program,"translucent_shadow_slot"))
    , translucent_shadow_image(gl::get_uniform_location(the_program,"translucent_shadow_image"))
    , translucent_shadow_near_range(gl::get_uniform_location(the_program,"translucent_shadow_near_range"))
    , translucent_shadow_angle_bias_scale(gl::get_uniform_location(the_program,"translucent_shadow_angle_bias_scale"))
    , translucent_sun(the_program)
{
    for(int i=0;i<4;++i) {
        translucent_spotlight_images.push_back(gl::get_uniform_location(
            the_program,"translucent_spotlight_image_"+std::to_string(i)));
        translucent_spotlight_origins.push_back(gl::get_uniform_location(
            the_program,"translucent_spotlight_origins["+std::to_string(i)+"]"));
        translucent_spotlight_rights.push_back(gl::get_uniform_location(
            the_program,"translucent_spotlight_rights["+std::to_string(i)+"]"));
        translucent_spotlight_ups.push_back(gl::get_uniform_location(
            the_program,"translucent_spotlight_ups["+std::to_string(i)+"]"));
        translucent_spotlight_forwards.push_back(gl::get_uniform_location(
            the_program,"translucent_spotlight_forwards["+std::to_string(i)+"]"));
        translucent_spotlight_intensities.push_back(gl::get_uniform_location(
            the_program,"translucent_spotlight_intensities["+std::to_string(i)+"]"));
        translucent_spotlight_bounds.push_back(gl::get_uniform_location(
            the_program,"translucent_spotlight_bounds["+std::to_string(i)+"]"));
    }
}


void jkgm::shaders::game_pass_shader::set_translucent_shadow(
    int slot,float near_plane,float far_plane,float angle_bias_scale)
{
    gl::set_uniform_integer(translucent_shadow_slot,slot);
    gl::set_uniform_float(translucent_shadow_angle_bias_scale,angle_bias_scale);
    // Bind its sampler type even when the light is inactive.
    gl::set_uniform_integer(translucent_shadow_image,
                            texture_units::forward_spotlight_shadow);
    if(slot<0)return;
    gl::set_uniform_vector(translucent_shadow_near_range,
                           make_point(near_plane,far_plane));
}

void jkgm::shaders::game_pass_shader::set_translucent_spotlight_count(int count)
{
    gl::set_uniform_integer(translucent_spotlight_count,count);
}

void jkgm::shaders::game_pass_shader::set_translucent_spotlight_camera(
    point<4,float> origin,point<4,float> right,point<4,float> forward,
    point<4,float> up,point<2,float> resolution)
{
    gl::set_uniform_vector(translucent_spotlight_camera_origin,origin);
    gl::set_uniform_vector(translucent_spotlight_camera_right,right);
    gl::set_uniform_vector(translucent_spotlight_camera_forward,forward);
    gl::set_uniform_vector(translucent_spotlight_camera_up,up);
    gl::set_uniform_vector(translucent_spotlight_resolution,resolution);
}

void jkgm::shaders::game_pass_shader::set_translucent_spotlight(
    int slot,point<4,float> origin,point<4,float> right,
    point<4,float> up,point<4,float> forward,
    point<2,float> strengths,point<4,float> bounds)
{
    if(slot<0 || slot>=4)return;
    gl::set_uniform_integer(translucent_spotlight_images[slot],12+slot);
    gl::set_uniform_vector(translucent_spotlight_origins[slot],origin);
    gl::set_uniform_vector(translucent_spotlight_rights[slot],right);
    gl::set_uniform_vector(translucent_spotlight_ups[slot],up);
    gl::set_uniform_vector(translucent_spotlight_forwards[slot],forward);
    gl::set_uniform_vector(translucent_spotlight_intensities[slot],strengths);
    gl::set_uniform_vector(translucent_spotlight_bounds[slot],bounds);
}

jkgm::shaders::gpu_3do_preview_shader::gpu_3do_preview_shader(
    fs::path const &root)
    : opengl_shader("gpu_3do_preview",root/"shaders/gpu_3do_preview.vert",
                   root/"shaders/gpu_3do_preview.frag")
    , view_right(gl::get_uniform_location(the_program,"view_right"))
    , view_forward(gl::get_uniform_location(the_program,"view_forward"))
    , view_up(gl::get_uniform_location(the_program,"view_up"))
    , view_origin(gl::get_uniform_location(the_program,"view_origin"))
    , sprite_instance_count(gl::get_uniform_location(
          the_program,"sprite_instance_count"))
    , sprite_instance_matrix(gl::get_uniform_location(
          the_program,"sprite_instance_matrix[0]"))
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
    , thing_sector_tint_delta(gl::get_uniform_location(the_program,"thing_sector_tint_delta"))
    , colormap_tint(gl::get_uniform_location(the_program,"colormap_tint"))
    , sector_crossing_count(gl::get_uniform_location(the_program,"sector_crossing_count"))
    , sector_crossing_per_vertex(gl::get_uniform_location(
          the_program,"sector_crossing_per_vertex"))
    , sector_crossing_planes(gl::get_uniform_location(the_program,"sector_crossing_planes"))
    , sector_crossing_camera_origin(gl::get_uniform_location(
          the_program,"sector_crossing_camera_origin"))
    , solid_fullbright(gl::get_uniform_location(the_program,"solid_fullbright"))
    , palette_fullbright_sampler(gl::get_uniform_location(the_program,"palette_fullbright_mask"))
    , has_palette_fullbright(gl::get_uniform_location(the_program,"has_palette_fullbright"))
    , specular_params(gl::get_uniform_location(the_program,"specular_params"))
    , specular_normal_mode(gl::get_uniform_location(the_program,"experimental_specular_normal_mode"))
    , normal_mapped_diffuse(gl::get_uniform_location(the_program,"normal_mapped_diffuse"))
    , dynamic_light_specular_enabled(gl::get_uniform_location(the_program,"dynamic_light_specular_enabled"))
    , specular_surface_sampler(gl::get_uniform_location(the_program,"surface_map"))
    , specular_normal_sampler(gl::get_uniform_location(the_program,"normal_map"))
    , specular_normal2_sampler(gl::get_uniform_location(the_program,"normal_map2"))
    , specular_environment_sampler(gl::get_uniform_location(the_program,"env_map"))
    , specular_environment2_sampler(gl::get_uniform_location(the_program,"env_map2"))
    , albedo2_sampler(gl::get_uniform_location(the_program,"albedo_map2"))
    , albedo_transition_info(gl::get_uniform_location(the_program,"albedo_transition"))
    , scene_depth_sampler(gl::get_uniform_location(the_program,"scene_depth"))
    , advanced_material_params(gl::get_uniform_location(the_program,"advanced_material_params"))
    , smoke_half_resolution(gl::get_uniform_location(the_program,"smoke_half_resolution"))
    , post_exempt_pass(gl::get_uniform_location(the_program,"post_exempt_pass"))
    , camera_hud_thing(gl::get_uniform_location(the_program,"camera_hud_thing"))
    , world_lighting_exempt(gl::get_uniform_location(
        the_program,"world_lighting_exempt"))
    , sprite_receiver(gl::get_uniform_location(the_program,"sprite_receiver"))
    , decal_light_texture(gl::get_uniform_location(the_program,"decal_light_texture"))
    , decal_occlusion(gl::get_uniform_location(the_program,"decal_occlusion"))
    , decal_receiver_lighting(gl::get_uniform_location(the_program,"decal_receiver_lighting"))
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
    , translucent_spotlight_count(gl::get_uniform_location(the_program,"translucent_spotlight_count"))
    , translucent_spotlight_resolution(gl::get_uniform_location(the_program,"translucent_spotlight_resolution"))
    , translucent_spotlight_camera_origin(gl::get_uniform_location(the_program,"translucent_spotlight_camera_origin"))
    , translucent_spotlight_camera_right(gl::get_uniform_location(the_program,"translucent_spotlight_camera_right"))
    , translucent_spotlight_camera_forward(gl::get_uniform_location(the_program,"translucent_spotlight_camera_forward"))
    , translucent_spotlight_camera_up(gl::get_uniform_location(the_program,"translucent_spotlight_camera_up"))
    , translucent_shadow_slot(gl::get_uniform_location(the_program,"translucent_shadow_slot"))
    , translucent_shadow_image(gl::get_uniform_location(the_program,"translucent_shadow_image"))
    , translucent_shadow_near_range(gl::get_uniform_location(the_program,"translucent_shadow_near_range"))
    , translucent_shadow_angle_bias_scale(gl::get_uniform_location(the_program,"translucent_shadow_angle_bias_scale"))
    , translucent_sun(the_program)
    , ambient_light(gl::get_uniform_location(the_program,"ambient_light"))
    , jk_light_limit(gl::get_uniform_location(the_program,"jk_light_limit"))
    , continuous_ambient_enabled(gl::get_uniform_location(
          the_program,"continuous_ambient_enabled"))
    , continuous_ambient_sector(gl::get_uniform_location(
          the_program,"continuous_ambient_sector"))
    , continuous_voxel_mask(gl::get_uniform_location(
          the_program,"continuous_voxel_mask"))
    , continuous_voxel_spacing(gl::get_uniform_location(
          the_program,"continuous_voxel_spacing"))
    , continuous_ambient_range(gl::get_uniform_location(
          the_program,"continuous_ambient_range"))
    , continuous_ambient_floor(gl::get_uniform_location(
          the_program,"continuous_ambient_floor"))
    , continuous_fallback_min(gl::get_uniform_location(
          the_program,"continuous_fallback_min"))
    , continuous_fallback_max(gl::get_uniform_location(
          the_program,"continuous_fallback_max"))
    , continuous_fallback_bottom(gl::get_uniform_location(
          the_program,"continuous_fallback_bottom"))
    , continuous_fallback_top(gl::get_uniform_location(
          the_program,"continuous_fallback_top"))
    , attenuation_min(gl::get_uniform_location(the_program,"attenuation_min"))
    , num_lights(gl::get_uniform_location(the_program,"num_lights"))
    , pixel_dynamic_lighting(gl::get_uniform_location(
          the_program,"pixel_dynamic_lighting"))
    , nonrigid_light_transform(gl::get_uniform_location(
          the_program,"nonrigid_light_transform"))
    , effective_light_mode(gl::get_uniform_location(the_program,"effective_light_mode"))
{
    for(unsigned i=0;i<4;i++) {
        auto const prefix=std::string("sector_crossing_");
        auto const suffix="["+std::to_string(i)+"]";
        sector_crossing_info.push_back(gl::get_uniform_location(
            the_program,prefix+"info"+suffix));
        sector_crossing_bounds_min.push_back(gl::get_uniform_location(
            the_program,prefix+"bounds_min"+suffix));
        sector_crossing_bounds_max.push_back(gl::get_uniform_location(
            the_program,prefix+"bounds_max"+suffix));
        sector_crossing_inner_min.push_back(gl::get_uniform_location(
            the_program,prefix+"inner_min"+suffix));
        sector_crossing_inner_max.push_back(gl::get_uniform_location(
            the_program,prefix+"inner_max"+suffix));
        sector_crossing_cmp.push_back(gl::get_uniform_location(
            the_program,prefix+"cmp"+suffix));
        sector_crossing_tint.push_back(gl::get_uniform_location(
            the_program,prefix+"tint"+suffix));
    }
    for(unsigned i=0;i<gpu_3do_max_lights;i++) {
        light_position_radius.push_back(gl::get_uniform_location(
            the_program,std::string("light_position_radius[")+std::to_string(i)+"]"));
        light_intensity.push_back(gl::get_uniform_location(
            the_program,std::string("light_intensity[")+std::to_string(i)+"]"));
        light_color.push_back(gl::get_uniform_location(
            the_program,std::string("light_color[")+std::to_string(i)+"]"));
    }

    for(int i=0;i<4;++i) {
        translucent_spotlight_images.push_back(gl::get_uniform_location(
            the_program,"translucent_spotlight_image_"+std::to_string(i)));
        translucent_spotlight_origins.push_back(gl::get_uniform_location(
            the_program,"translucent_spotlight_origins["+std::to_string(i)+"]"));
        translucent_spotlight_rights.push_back(gl::get_uniform_location(
            the_program,"translucent_spotlight_rights["+std::to_string(i)+"]"));
        translucent_spotlight_ups.push_back(gl::get_uniform_location(
            the_program,"translucent_spotlight_ups["+std::to_string(i)+"]"));
        translucent_spotlight_forwards.push_back(gl::get_uniform_location(
            the_program,"translucent_spotlight_forwards["+std::to_string(i)+"]"));
        translucent_spotlight_intensities.push_back(gl::get_uniform_location(
            the_program,"translucent_spotlight_intensities["+std::to_string(i)+"]"));
        translucent_spotlight_bounds.push_back(gl::get_uniform_location(
            the_program,"translucent_spotlight_bounds["+std::to_string(i)+"]"));
    }
}

void jkgm::shaders::gpu_3do_preview_shader::set_translucent_shadow(
    int slot,float near_plane,float far_plane,float angle_bias_scale)
{
    gl::set_uniform_integer(translucent_shadow_slot,slot);
    gl::set_uniform_float(translucent_shadow_angle_bias_scale,angle_bias_scale);
    // Bind its sampler type even when the light is inactive.
    gl::set_uniform_integer(translucent_shadow_image,
                            texture_units::forward_spotlight_shadow);
    if(slot<0)return;
    gl::set_uniform_vector(translucent_shadow_near_range,
                           make_point(near_plane,far_plane));
}

void jkgm::shaders::gpu_3do_preview_shader::set_translucent_spotlight_count(int count)
{
    gl::set_uniform_integer(translucent_spotlight_count,count);
}

void jkgm::shaders::gpu_3do_preview_shader::set_translucent_spotlight_camera(
    point<4,float> origin,point<4,float> right,point<4,float> forward,
    point<4,float> up,point<2,float> resolution)
{
    gl::set_uniform_vector(translucent_spotlight_camera_origin,origin);
    gl::set_uniform_vector(translucent_spotlight_camera_right,right);
    gl::set_uniform_vector(translucent_spotlight_camera_forward,forward);
    gl::set_uniform_vector(translucent_spotlight_camera_up,up);
    gl::set_uniform_vector(translucent_spotlight_resolution,resolution);
}

void jkgm::shaders::gpu_3do_preview_shader::set_translucent_spotlight(
    int slot,point<4,float> origin,point<4,float> right,
    point<4,float> up,point<4,float> forward,
    point<2,float> strengths,point<4,float> bounds)
{
    if(slot<0 || slot>=4)return;
    gl::set_uniform_integer(translucent_spotlight_images[slot],12+slot);
    gl::set_uniform_vector(translucent_spotlight_origins[slot],origin);
    gl::set_uniform_vector(translucent_spotlight_rights[slot],right);
    gl::set_uniform_vector(translucent_spotlight_ups[slot],up);
    gl::set_uniform_vector(translucent_spotlight_forwards[slot],forward);
    gl::set_uniform_vector(translucent_spotlight_intensities[slot],strengths);
    gl::set_uniform_vector(translucent_spotlight_bounds[slot],bounds);
}

void jkgm::shaders::gpu_3do_preview_shader::set_view_matrix(
    point<4,float> r,point<4,float> f,point<4,float> u,point<4,float> t)
{
    gl::set_uniform_vector(view_right,r);
    gl::set_uniform_vector(view_forward,f);
    gl::set_uniform_vector(view_up,u);
    gl::set_uniform_vector(view_origin,t);
}
void jkgm::shaders::gpu_3do_preview_shader::set_sprite_instances(
    std::array<std::array<float,12>,max_sprite_instances> const &models,
    size_t count)
{
    if(count==0 || count>max_sprite_instances) {
        gl::set_uniform_integer(sprite_instance_count,0);
        return;
    }
    // One uniform-array upload per batch, independent of the per-Thing VAO.
    std::array<float,max_sprite_instances*16> packed{};
    for(size_t i=0;i<count;++i) {
        auto const &m=models[i];
        for(size_t axis=0;axis<4;++axis) {
            size_t const dst=i*16+axis*4;
            packed[dst+0]=m[axis*3+0];
            packed[dst+1]=m[axis*3+1];
            packed[dst+2]=m[axis*3+2];
            packed[dst+3]=axis==3?1.0f:0.0f;
        }
    }
    glUniform4fv(sprite_instance_matrix.get(),
                 static_cast<GLsizei>(count*4U),packed.data());
    gl::set_uniform_integer(sprite_instance_count,static_cast<int>(count));
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

void jkgm::shaders::gpu_3do_preview_shader::set_continuous_ambient(
    gpu_3do_ambient_volume const *fallback,
    uint32_t sector,uint32_t mask,float spacing)
{
    bool const active=fallback && sector!=0xffffffffU &&
        mask!=0U && spacing>0.0f && std::isfinite(spacing);
    gl::set_uniform_integer(continuous_ambient_enabled,active?1:0);
    if(!active)return;
    gl::set_uniform_integer(continuous_ambient_sector,static_cast<int>(sector));
    gl::set_uniform_integer(continuous_voxel_mask,static_cast<int>(mask));
    gl::set_uniform_float(continuous_voxel_spacing,spacing);
    gl::set_uniform_float(continuous_ambient_range,fallback->range);
    gl::set_uniform_float(continuous_ambient_floor,fallback->live_floor);
    gl::set_uniform_vector(continuous_fallback_min,
        make_point(fallback->bounds_min[0],fallback->bounds_min[1],
                   fallback->bounds_min[2]));
    gl::set_uniform_vector(continuous_fallback_max,
        make_point(fallback->bounds_max[0],fallback->bounds_max[1],
                   fallback->bounds_max[2]));
    auto q=[&](size_t i){return float(fallback->samples[i])/255.0f;};
    gl::set_uniform_vector(continuous_fallback_bottom,
        make_point(q(0),q(1),q(2),q(3)));
    gl::set_uniform_vector(continuous_fallback_top,
        make_point(q(4),q(5),q(6),q(7)));
}

void jkgm::shaders::gpu_3do_preview_shader::set_sector_crossing(
    gpu_3do_crossing_state const *state,bool enabled,
    bool allow_sector_tint,bool per_vertex)
{
    int const count=enabled && state && state->count>1U &&
        state->count<=4U ? static_cast<int>(state->count) : 0;
    // A samplerBuffer MUST have a different texture unit than sampler2D
    // even when the feature is off: GLSL draw validation checks sampler
    // type collisions before evaluating any conditional texture lookups.
    gl::set_uniform_integer(
        sector_crossing_planes,gpu_3do_sector_plane_texture_unit);
    gl::set_uniform_integer(sector_crossing_count,count);
    gl::set_uniform_integer(sector_crossing_per_vertex,
        count>1 && per_vertex ? 1 : 0);
    if(!count)return;
    for(int i=0;i<count;++i) {
        auto const &s=state->sectors[i];
        gl::set_uniform_vector(sector_crossing_info[i],
            make_point(s.ambient,float(s.plane_start),
                       float(s.plane_count),s.interior_valid?1.0f:0.0f));
        gl::set_uniform_vector(sector_crossing_bounds_min[i],
            make_point(s.bounds_min[0],s.bounds_min[1],s.bounds_min[2],0.0f));
        gl::set_uniform_vector(sector_crossing_bounds_max[i],
            make_point(s.bounds_max[0],s.bounds_max[1],s.bounds_max[2],0.0f));
        gl::set_uniform_vector(sector_crossing_inner_min[i],
            make_point(s.interior_min[0],s.interior_min[1],s.interior_min[2],0.0f));
        gl::set_uniform_vector(sector_crossing_inner_max[i],
            make_point(s.interior_max[0],s.interior_max[1],s.interior_max[2],0.0f));
        gl::set_uniform_vector(sector_crossing_cmp[i],
            make_point(s.cmp_tint[0],s.cmp_tint[1],s.cmp_tint[2],1.0f));
        auto const tint=allow_sector_tint
            ? s.sector_tint : std::array<float,3>{{0.0f,0.0f,0.0f}};
        gl::set_uniform_vector(sector_crossing_tint[i],
            make_point(tint[0],tint[1],tint[2],0.0f));
    }
}
void jkgm::shaders::gpu_3do_preview_shader::set_sector_crossing_camera_origin(
    std::array<float,3> const &origin)
{
    gl::set_uniform_vector(sector_crossing_camera_origin,
        make_point(origin[0],origin[1],origin[2]));
}

void jkgm::shaders::gpu_3do_preview_shader::set_thing_sector_tint_delta(
    std::array<float,3> const &delta)
{
    gl::set_uniform_vector(thing_sector_tint_delta,
        make_point(delta[0],delta[1],delta[2]));
}

void jkgm::shaders::gpu_3do_preview_shader::set_solid_fullbright(bool enabled)
{
    gl::set_uniform_integer(solid_fullbright,enabled?1:0);
}
void jkgm::shaders::gpu_3do_preview_shader::set_palette_fullbright(bool enabled)
{
    gl::set_uniform_integer(palette_fullbright_sampler,11);
    gl::set_uniform_integer(has_palette_fullbright,enabled?1:0);
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

void jkgm::shaders::gpu_3do_preview_shader::set_specular_normal_mode(
    int mode)
{
    gl::set_uniform_integer(specular_normal_mode,mode);
}

void jkgm::shaders::gpu_3do_preview_shader::set_dynamic_light_specular(bool enabled)
{
    gl::set_uniform_integer(dynamic_light_specular_enabled,enabled?1:0);
}

void jkgm::shaders::gpu_3do_preview_shader::set_normal_mapped_diffuse(
    bool enabled)
{
    gl::set_uniform_integer(normal_mapped_diffuse,enabled?1:0);
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

void jkgm::shaders::gpu_3do_preview_shader::set_smoke_half_resolution(bool enabled)
{
    gl::set_uniform_integer(smoke_half_resolution,enabled?1:0);
}

void jkgm::shaders::gpu_3do_preview_shader::set_world_lighting_exempt(
    bool enabled)
{
    gl::set_uniform_integer(world_lighting_exempt,enabled?1:0);
}

void jkgm::shaders::gpu_3do_preview_shader::set_sprite_receiver(bool enabled)
{
    gl::set_uniform_integer(sprite_receiver,enabled?1:0);
}

void jkgm::shaders::gpu_3do_preview_shader::set_post_exempt(bool enabled)
{
    gl::set_uniform_integer(post_exempt_pass,enabled?1:0);
}

void jkgm::shaders::gpu_3do_preview_shader::set_camera_hud_thing(
    bool enabled,bool world_depth)
{
    // 0=ordinary Thing, 1=always-on-top HUD, 2=HUD occluded by world.
    gl::set_uniform_integer(camera_hud_thing,enabled?(world_depth?2:1):0);
}

void jkgm::shaders::gpu_3do_preview_shader::set_decal_receiver_lighting(
    bool enabled,int light,int ao)
{
    gl::set_uniform_integer(decal_receiver_lighting,enabled?1:0);
    if(enabled) {
        gl::set_uniform_integer(decal_light_texture,light);
        gl::set_uniform_integer(decal_occlusion,ao);
    }
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

void jkgm::shaders::gpu_3do_preview_shader::set_light_brightness_limit(
    float limit)
{
    gl::set_uniform_float(jk_light_limit,limit);
}
void jkgm::shaders::gpu_3do_preview_shader::set_lighting(
    gpu_3do_instance const &instance,bool pixel_lighting,
    std::array<std::array<float,3>,64> const &saber_rgb,
    std::array<bool,64> const &saber_valid)
{
    gl::set_uniform_float(ambient_light,instance.ambient);
    gl::set_uniform_float(attenuation_min,instance.attenuation);
    gl::set_uniform_integer(num_lights,static_cast<int>(instance.num_lights));
    gl::set_uniform_integer(pixel_dynamic_lighting,pixel_lighting?1:0);
    gl::set_uniform_integer(nonrigid_light_transform,
        instance.nonrigid_light_transform ? 1 : 0);
    gl::set_uniform_float(effective_light_mode,instance.effective_light_mode);
    for(unsigned i=0;i<instance.num_lights;i++) {
        auto const &light=instance.lights[i];
        gl::set_uniform_vector(light_position_radius[i],
            make_point(light.local_position[0],light.local_position[1],
                       light.local_position[2],light.radius));
        gl::set_uniform_float(light_intensity[i],light.intensity);
        unsigned const id=light.native_id;
        auto const rgb=id<64U && saber_valid[id]
            ?saber_rgb[id]:std::array<float,3>{{1.f,1.f,1.f}};
        gl::set_uniform_vector(light_color[i],
            make_point(rgb[0],rgb[1],rgb[2]));
    }
}

jkgm::shaders::game_opaque_pass_shader::game_opaque_pass_shader(
    fs::path const &data_root)
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

void jkgm::shaders::game_pass_shader::set_specular_normal_mode(
    int mode)
{
    gl::set_uniform_integer(specular_normal_mode,mode);
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

jkgm::shaders::game_post_ssao_shader::game_post_ssao_shader(
    fs::path const &data_root,bool half_res)
    : opengl_shader(half_res?"game_post_ssao_half":"game_post_ssao",
                    data_root / "shaders/postprocess.vert",
                    data_root / (half_res?"shaders/post_ssao_half.frag":
                                          "shaders/post_ssao.frag"))
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


jkgm::shaders::spotlight_shadow_depth_shader::spotlight_shadow_depth_shader(
    fs::path const &data_root)
    : opengl_shader("spotlight_shadow",
        data_root/"shaders/spotlight_shadow.vert",
        data_root/"shaders/spotlight_shadow.frag")
    , shadow_origin(gl::get_uniform_location(the_program,"shadow_origin"))
    , shadow_right(gl::get_uniform_location(the_program,"shadow_right"))
    , shadow_up(gl::get_uniform_location(the_program,"shadow_up"))
    , shadow_forward(gl::get_uniform_location(the_program,"shadow_forward"))
    , shadow_limits(gl::get_uniform_location(the_program,"shadow_limits"))
    , shadow_orthographic(gl::get_uniform_location(the_program,"shadow_orthographic"))
    , shadow_model_right(gl::get_uniform_location(the_program,"shadow_model_right"))
    , shadow_model_forward(gl::get_uniform_location(the_program,"shadow_model_forward"))
    , shadow_model_up(gl::get_uniform_location(the_program,"shadow_model_up"))
    , shadow_model_origin(gl::get_uniform_location(the_program,"shadow_model_origin"))
    , shadow_instance_count(gl::get_uniform_location(the_program,"shadow_instance_count"))
    , shadow_instance_right(gl::get_uniform_location(the_program,"shadow_instance_right[0]"))
    , shadow_instance_forward(gl::get_uniform_location(the_program,"shadow_instance_forward[0]"))
    , shadow_instance_up(gl::get_uniform_location(the_program,"shadow_instance_up[0]"))
    , shadow_instance_origin(gl::get_uniform_location(the_program,"shadow_instance_origin[0]"))
    , shadow_use_alpha(gl::get_uniform_location(the_program,"shadow_use_alpha"))
    , shadow_alpha_texture(gl::get_uniform_location(the_program,"shadow_alpha_texture"))
    , shadow_alpha_texture_next(gl::get_uniform_location(the_program,"shadow_alpha_texture_next"))
    , shadow_alpha_uv_scale(gl::get_uniform_location(the_program,"shadow_alpha_uv_scale"))
    , shadow_alpha_uv_scroll(gl::get_uniform_location(the_program,"shadow_alpha_uv_scroll"))
    , shadow_alpha_transition(gl::get_uniform_location(the_program,"shadow_alpha_transition"))
    , shadow_alpha_cutoff(gl::get_uniform_location(the_program,"shadow_alpha_cutoff"))
    , shadow_alpha_factor(gl::get_uniform_location(the_program,"shadow_alpha_factor"))
{
}

void jkgm::shaders::spotlight_shadow_depth_shader::set_light(
    point<3,float> origin,point<3,float> right,point<3,float> up,
    point<3,float> forward,point<4,float> limits)
{
    gl::set_uniform_integer(shadow_orthographic,0);
    gl::set_uniform_vector(shadow_origin,origin);
    gl::set_uniform_vector(shadow_right,right);
    gl::set_uniform_vector(shadow_up,up);
    gl::set_uniform_vector(shadow_forward,forward);
    gl::set_uniform_vector(shadow_limits,limits);
}

void jkgm::shaders::spotlight_shadow_depth_shader::set_orthographic_light(
    point<3,float> origin,point<3,float> right,point<3,float> up,
    point<3,float> forward,point<4,float> limits)
{
    gl::set_uniform_integer(shadow_orthographic,1);
    gl::set_uniform_vector(shadow_origin,origin);
    gl::set_uniform_vector(shadow_right,right);
    gl::set_uniform_vector(shadow_up,up);
    gl::set_uniform_vector(shadow_forward,forward);
    gl::set_uniform_vector(shadow_limits,limits);
}

void jkgm::shaders::spotlight_shadow_depth_shader::set_model(
    point<3,float> right,point<3,float> forward,
    point<3,float> up,point<3,float> origin)
{
    if(shadow_instance_active) {
        gl::set_uniform_integer(shadow_instance_count,0);
        shadow_instance_active=false;
    }
    gl::set_uniform_vector(shadow_model_right,right);
    gl::set_uniform_vector(shadow_model_forward,forward);
    gl::set_uniform_vector(shadow_model_up,up);
    gl::set_uniform_vector(shadow_model_origin,origin);
}

void jkgm::shaders::spotlight_shadow_depth_shader::set_instanced_models(
    std::array<std::array<float,12>,max_shadow_instances> const &models,
    size_t count)
{
    // One uniform-array upload per axis, rather than four uniform updates
    // per Thing and a draw call per material group. Count is capped so the
    // GLSL 3.3 implementation stays within minimum vertex-uniform limits.
    if(count==0 || count>max_shadow_instances) {
        if(shadow_instance_active) {
            gl::set_uniform_integer(shadow_instance_count,0);
            shadow_instance_active=false;
        }
        return;
    }
    std::array<float,3*max_shadow_instances> right{},forward{},up{},origin{};
    for(size_t i=0;i<count;++i) {
        auto const &m=models[i];
        for(size_t axis=0;axis<3;++axis) {
            right[i*3+axis]=m[axis];
            forward[i*3+axis]=m[axis+3];
            up[i*3+axis]=m[axis+6];
            origin[i*3+axis]=m[axis+9];
        }
    }
    glUniform3fv(shadow_instance_right.get(),static_cast<GLsizei>(count),
                 right.data());
    glUniform3fv(shadow_instance_forward.get(),static_cast<GLsizei>(count),
                 forward.data());
    glUniform3fv(shadow_instance_up.get(),static_cast<GLsizei>(count),
                 up.data());
    glUniform3fv(shadow_instance_origin.get(),static_cast<GLsizei>(count),
                 origin.data());
    gl::set_uniform_integer(shadow_instance_count,static_cast<int>(count));
    shadow_instance_active=true;
}

void jkgm::shaders::spotlight_shadow_depth_shader::set_shadow_alpha(
    bool enabled,point<2,float> uv_scale,point<2,float> uv_scroll,
    float cutoff,float factor,bool has_next,float progress)
{
    gl::set_uniform_integer(shadow_use_alpha,enabled?1:0);
    if(!enabled)return;
    gl::set_uniform_integer(shadow_alpha_texture,11);
    gl::set_uniform_integer(shadow_alpha_texture_next,12);
    gl::set_uniform_vector(shadow_alpha_uv_scale,uv_scale);
    gl::set_uniform_vector(shadow_alpha_uv_scroll,uv_scroll);
    gl::set_uniform_float(shadow_alpha_cutoff,cutoff);
    gl::set_uniform_float(shadow_alpha_factor,factor);
    gl::set_uniform_vector(shadow_alpha_transition,
        make_point(has_next?1.0f:0.0f,progress));
}

jkgm::shaders::game_post_opaque_composite_shader::game_post_opaque_composite_shader(
    fs::path const &data_root,bool half_sun)
    : opengl_shader(half_sun?"game_post_opaque_half_sun":"game_post_opaque_composite",
                    data_root / "shaders/postprocess.vert",
                    data_root / (half_sun?"shaders/post_sun_visibility_half.frag":
                                          "shaders/post_opaque_composite.frag"))
    , color_image(gl::get_uniform_location(the_program, "color_image"))
    , emissive_image(gl::get_uniform_location(the_program, "emissive_image"))
    , occlusion_image(gl::get_uniform_location(the_program, "occlusion_image"))
    , depth_image(gl::get_uniform_location(the_program, "depth_image"))
    , debug_void_pixels(gl::get_uniform_location(the_program, "debug_void_pixels"))
    , retain_hom_pixels(gl::get_uniform_location(the_program, "retain_hom_pixels"))
    , fog_params(gl::get_uniform_location(the_program, "fog_params"))
    , fog_color(gl::get_uniform_location(the_program, "fog_color"))
    , normal_image(gl::get_uniform_location(the_program, "normal_image"))
    , camera_origin(gl::get_uniform_location(the_program, "camera_origin"))
    , camera_right(gl::get_uniform_location(the_program, "camera_right"))
    , camera_forward(gl::get_uniform_location(the_program, "camera_forward"))
    , camera_up(gl::get_uniform_location(the_program, "camera_up"))
    , spotlight_count(gl::get_uniform_location(the_program,"spotlight_count"))
    , spotlight_shadow_slot(gl::get_uniform_location(the_program,"spotlight_shadow_slot"))
    , spotlight_shadow_image(gl::get_uniform_location(the_program,"spotlight_shadow_image"))
    , spotlight_shadow_near_range(gl::get_uniform_location(the_program,"spotlight_shadow_near_range"))
    , spotlight_shadow_angle_bias_scale(gl::get_uniform_location(the_program,"spotlight_shadow_angle_bias_scale"))
    , sun_lighting_active(gl::get_uniform_location(the_program,"sun_lighting_active"))
    , sun_shadow_active(gl::get_uniform_location(the_program,"sun_shadow_active"))
    , sun_shadow_image(gl::get_uniform_location(the_program,"sun_shadow_image"))
    , sun_shadow_direction(gl::get_uniform_location(the_program,"sun_shadow_direction"))
    , sun_shadow_color_intensity(gl::get_uniform_location(the_program,"sun_shadow_color_intensity"))
    , sun_shadow_origin(gl::get_uniform_location(the_program,"sun_shadow_origin"))
    , sun_shadow_right(gl::get_uniform_location(the_program,"sun_shadow_right"))
    , sun_shadow_up(gl::get_uniform_location(the_program,"sun_shadow_up"))
    , sun_shadow_forward(gl::get_uniform_location(the_program,"sun_shadow_forward"))
    , sun_shadow_limits(gl::get_uniform_location(the_program,"sun_shadow_limits"))
    , sun_shadow_far_image(gl::get_uniform_location(the_program,"sun_shadow_far_image"))
    , sun_shadow_far_origin(gl::get_uniform_location(the_program,"sun_shadow_far_origin"))
    , sun_shadow_far_right(gl::get_uniform_location(the_program,"sun_shadow_far_right"))
    , sun_shadow_far_up(gl::get_uniform_location(the_program,"sun_shadow_far_up"))
    , sun_shadow_far_forward(gl::get_uniform_location(the_program,"sun_shadow_far_forward"))
    , sun_shadow_far_limits(gl::get_uniform_location(the_program,"sun_shadow_far_limits"))
    , sun_shadow_split(gl::get_uniform_location(the_program,"sun_shadow_split"))
    , sun_shadow_receiver_bias_scale(gl::get_uniform_location(
        the_program,"sun_shadow_receiver_bias_scale"))
    , sun_shadow_pcf_radius(gl::get_uniform_location(
        the_program,"sun_shadow_pcf_radius"))
    , sun_sky_aperture_active(gl::get_uniform_location(
        the_program,"sun_sky_aperture_active"))
    , sun_sky_aperture_map(gl::get_uniform_location(
        the_program,"sun_sky_aperture_map"))
    , sun_sky_aperture_depth_transform(gl::get_uniform_location(
        the_program,"sun_sky_aperture_depth_transform"))
    , sun_max_distance(gl::get_uniform_location(the_program,"sun_max_distance"))
    , sun_half_visibility_active(gl::get_uniform_location(
        the_program,"sun_half_visibility_active"))
    , sun_half_visibility_image(gl::get_uniform_location(
        the_program,"sun_half_visibility_image"))
    , sun_dynamic_count(gl::get_uniform_location(the_program,"sun_dynamic_count"))
    , sun_dynamic_image(gl::get_uniform_location(the_program,"sun_dynamic_image"))
    , sun_dynamic_pcf_radius(gl::get_uniform_location(
        the_program,"sun_dynamic_pcf_radius"))
 
{
    for(int i=0;i<8;++i) {
        sun_dynamic_atlas_rects.push_back(gl::get_uniform_location(
            the_program,"sun_dynamic_atlas_rects["+std::to_string(i)+"]"));
        sun_dynamic_origin.push_back(gl::get_uniform_location(
            the_program,"sun_dynamic_origin["+std::to_string(i)+"]"));
        sun_dynamic_right.push_back(gl::get_uniform_location(
            the_program,"sun_dynamic_right["+std::to_string(i)+"]"));
        sun_dynamic_up.push_back(gl::get_uniform_location(
            the_program,"sun_dynamic_up["+std::to_string(i)+"]"));
        sun_dynamic_forward.push_back(gl::get_uniform_location(
            the_program,"sun_dynamic_forward["+std::to_string(i)+"]"));
        sun_dynamic_limits.push_back(gl::get_uniform_location(
            the_program,"sun_dynamic_limits["+std::to_string(i)+"]"));
    }
    for(int i=0;i<4;++i) {
        spotlight_images.push_back(gl::get_uniform_location(
            the_program,"spotlight_image_"+std::to_string(i)));
        spotlight_origins.push_back(gl::get_uniform_location(
            the_program,"spotlight_origins["+std::to_string(i)+"]"));
        spotlight_rights.push_back(gl::get_uniform_location(
            the_program,"spotlight_rights["+std::to_string(i)+"]"));
        spotlight_ups.push_back(gl::get_uniform_location(
            the_program,"spotlight_ups["+std::to_string(i)+"]"));
        spotlight_forwards.push_back(gl::get_uniform_location(
            the_program,"spotlight_forwards["+std::to_string(i)+"]"));
        spotlight_strengths.push_back(gl::get_uniform_location(
            the_program,"spotlight_intensities["+std::to_string(i)+"]"));
        spotlight_screen_bounds.push_back(gl::get_uniform_location(
            the_program,"spotlight_screen_bounds["+std::to_string(i)+"]"));
    }
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

void jkgm::shaders::game_post_opaque_composite_shader::set_coverage_normal_sampler(
    int sampler)
{
    gl::set_uniform_integer(normal_image,sampler);
}

void jkgm::shaders::game_post_opaque_composite_shader::set_debug_void_pixels(
    bool enabled)
{
    gl::set_uniform_integer(debug_void_pixels,enabled?1:0);
}

void jkgm::shaders::game_post_opaque_composite_shader::set_retain_hom_pixels(
    bool enabled)
{
    gl::set_uniform_integer(retain_hom_pixels,enabled?1:0);
}

void jkgm::shaders::game_post_opaque_composite_shader::set_fog(point<4,float> params,
                                                                color_rgb fog)
{
    gl::set_uniform_vector(fog_params,params);
    gl::set_uniform_vector(fog_color,fog);
}

void jkgm::shaders::game_post_opaque_composite_shader::set_projector_camera(
    point<4,float> origin,point<4,float> right,point<4,float> forward,
    point<4,float> up)
{
    // The normal/coverage sampler is bound separately on every opaque pass.
    gl::set_uniform_vector(camera_origin,origin);
    gl::set_uniform_vector(camera_right,right);
    gl::set_uniform_vector(camera_forward,forward);
    gl::set_uniform_vector(camera_up,up);
}

void jkgm::shaders::game_post_opaque_composite_shader::set_spotlight_count(int count)
{
    gl::set_uniform_integer(spotlight_count,count);
}

void jkgm::shaders::game_post_opaque_composite_shader::set_spotlight_shadow(
    int slot,float near_plane,float far_plane,float angle_bias_scale)
{
    gl::set_uniform_integer(spotlight_shadow_slot,slot);
    gl::set_uniform_float(spotlight_shadow_angle_bias_scale,angle_bias_scale);
    gl::set_uniform_integer(spotlight_shadow_image,10);
    if(slot>=0) {
        gl::set_uniform_vector(spotlight_shadow_near_range,
                               make_point(near_plane,far_plane));
    }
}

void jkgm::shaders::game_post_opaque_composite_shader::set_sun_shadow(
    bool lighting_enabled,bool shadows_enabled,
    point<3,float> direction,point<4,float> color_intensity,
    point<3,float> origin,point<3,float> right,point<3,float> up,
    point<3,float> forward,point<3,float> limits)
{
    gl::set_uniform_integer(sun_lighting_active,lighting_enabled?1:0);
    gl::set_uniform_integer(sun_shadow_active,shadows_enabled?1:0);
    if(!lighting_enabled)return;
    gl::set_uniform_vector(sun_shadow_direction,direction);
    gl::set_uniform_vector(sun_shadow_color_intensity,color_intensity);
    if(!shadows_enabled)return;
    gl::set_uniform_integer(sun_shadow_image,13);
    gl::set_uniform_vector(sun_shadow_origin,origin);
    gl::set_uniform_vector(sun_shadow_right,right);
    gl::set_uniform_vector(sun_shadow_up,up);
    gl::set_uniform_vector(sun_shadow_forward,forward);
    gl::set_uniform_vector(sun_shadow_limits,limits);
}

void jkgm::shaders::game_post_opaque_composite_shader::set_sun_shadow_far(
    point<3,float> origin,point<3,float> right,point<3,float> up,
    point<3,float> forward,point<3,float> limits,float split)
{
    gl::set_uniform_integer(sun_shadow_far_image,14);
    gl::set_uniform_vector(sun_shadow_far_origin,origin);
    gl::set_uniform_vector(sun_shadow_far_right,right);
    gl::set_uniform_vector(sun_shadow_far_up,up);
    gl::set_uniform_vector(sun_shadow_far_forward,forward);
    gl::set_uniform_vector(sun_shadow_far_limits,limits);
    gl::set_uniform_float(sun_shadow_split,split);
}

void jkgm::shaders::game_post_opaque_composite_shader::set_sun_shadow_receiver_bias_scale(
    float scale)
{
    gl::set_uniform_float(sun_shadow_receiver_bias_scale,scale);
}

void jkgm::shaders::game_post_opaque_composite_shader::set_sun_shadow_pcf_radius(
    float near_radius,float far_radius)
{
    gl::set_uniform_vector(sun_shadow_pcf_radius,
                           make_point(near_radius,far_radius));
}

void jkgm::shaders::game_post_opaque_composite_shader::set_sun_dynamic_pcf_radius(
    float radius)
{
    gl::set_uniform_float(sun_dynamic_pcf_radius,radius);
}

void jkgm::shaders::game_post_opaque_composite_shader::set_sun_sky_aperture(
    bool active,int sampler,point<4,float> near_far_depth_transform)
{
    gl::set_uniform_integer(sun_sky_aperture_map,sampler);
    gl::set_uniform_integer(sun_sky_aperture_active,active?1:0);
    if(!active)return;
    gl::set_uniform_vector(sun_sky_aperture_depth_transform,
                           near_far_depth_transform);
}

void jkgm::shaders::game_post_opaque_composite_shader::set_sun_max_distance(
    float distance)
{
    gl::set_uniform_float(sun_max_distance,distance);
}

void jkgm::shaders::game_post_opaque_composite_shader::set_sun_half_visibility(
    bool active,int sampler)
{
    gl::set_uniform_integer(sun_half_visibility_image,sampler);
    gl::set_uniform_integer(sun_half_visibility_active,active?1:0);
}

void jkgm::shaders::game_post_opaque_composite_shader::set_sun_dynamic_count(
    int count)
{
    gl::set_uniform_integer(sun_dynamic_count,count);
    if(count>0)gl::set_uniform_integer(sun_dynamic_image,15);
}

void jkgm::shaders::game_post_opaque_composite_shader::set_sun_dynamic_atlas_rect(
    int slot,point<4,float> rect)
{
    if(slot<0 || slot>=8)return;
    gl::set_uniform_vector(sun_dynamic_atlas_rects[slot],rect);
}

void jkgm::shaders::game_post_opaque_composite_shader::set_sun_dynamic(
    int slot,point<3,float> origin,point<3,float> right,point<3,float> up,
    point<3,float> forward,point<3,float> limits)
{
    if(slot<0 || slot>=8)return;
    gl::set_uniform_vector(sun_dynamic_origin[slot],origin);
    gl::set_uniform_vector(sun_dynamic_right[slot],right);
    gl::set_uniform_vector(sun_dynamic_up[slot],up);
    gl::set_uniform_vector(sun_dynamic_forward[slot],forward);
    gl::set_uniform_vector(sun_dynamic_limits[slot],limits);
}

void jkgm::shaders::game_post_opaque_composite_shader::set_spotlight(
    int slot,point<4,float> origin,point<4,float> right,point<4,float> up,
    point<4,float> forward,point<2,float> intensities,
    point<4,float> screen_bounds)
{
    if(slot<0 || slot>=4)return;
    gl::set_uniform_integer(spotlight_images[slot],6+slot);
    gl::set_uniform_vector(spotlight_origins[slot],origin);
    gl::set_uniform_vector(spotlight_rights[slot],right);
    gl::set_uniform_vector(spotlight_ups[slot],up);
    gl::set_uniform_vector(spotlight_forwards[slot],forward);
    gl::set_uniform_vector(spotlight_strengths[slot],intensities);
    gl::set_uniform_vector(spotlight_screen_bounds[slot],screen_bounds);
}

jkgm::shaders::game_transparency_pass_shader::game_transparency_pass_shader(
    fs::path const &data_root)
    : game_pass_shader("game_transparency_pass",
                       data_root / "shaders/game.vert",
                       data_root / "shaders/game_trns_pass.frag")
{
}

jkgm::shaders::post_ssao_half_blur_shader::post_ssao_half_blur_shader(
    fs::path const &data_root)
    : opengl_shader("post_ssao_half_blur",
                    data_root/"shaders/postprocess.vert",
                    data_root/"shaders/post_ssao_half_blur.frag")
    , source_image(gl::get_uniform_location(the_program,"source_image"))
{}
void jkgm::shaders::post_ssao_half_blur_shader::set_source_sampler(int s)
{
    gl::set_uniform_integer(source_image,s);
}

jkgm::shaders::post_ssao_half_upsample_shader::
    post_ssao_half_upsample_shader(fs::path const &data_root)
    : opengl_shader("post_ssao_half_upsample",
                    data_root/"shaders/postprocess.vert",
                    data_root/"shaders/post_ssao_half_upsample.frag")
    , source_image(gl::get_uniform_location(the_program,"source_image"))
    , normal_image(gl::get_uniform_location(the_program,"normal_image"))
    , depth_image(gl::get_uniform_location(the_program,"depth_image"))
{}
void jkgm::shaders::post_ssao_half_upsample_shader::set_source_sampler(int s)
{
    gl::set_uniform_integer(source_image,s);
}
void jkgm::shaders::post_ssao_half_upsample_shader::set_normal_sampler(int s)
{
    gl::set_uniform_integer(normal_image,s);
}
void jkgm::shaders::post_ssao_half_upsample_shader::set_depth_sampler(int s)
{
    gl::set_uniform_integer(depth_image,s);
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
    , exposure_image(gl::get_uniform_location(the_program,"exposure_image"))
    , exposure_enabled(gl::get_uniform_location(the_program,"exposure_enabled"))
    , hdr_exposure_compensation(gl::get_uniform_location(the_program,"hdr_exposure_compensation"))
{
}

void jkgm::shaders::post_low_pass_shader::set_fbuf_sampler(int sampler)
{
    gl::set_uniform_integer(fbuf_image, sampler);
}

void jkgm::shaders::post_low_pass_shader::set_exposure(
    int sampler,bool adaptive,float compensation_ev)
{
    gl::set_uniform_integer(exposure_image,sampler);
    gl::set_uniform_integer(exposure_enabled,adaptive?1:0);
    gl::set_uniform_float(hdr_exposure_compensation,compensation_ev);
}

jkgm::shaders::post_smoke_composite_shader::post_smoke_composite_shader(
    fs::path const &data_root)
    : opengl_shader("post_smoke_composite",
                    data_root/"shaders/postprocess.vert",
                    data_root/"shaders/post_smoke_composite.frag")
    , smoke_image(gl::get_uniform_location(the_program,"smoke_image"))
    , opaque_depth(gl::get_uniform_location(the_program,"opaque_depth"))
{
}

void jkgm::shaders::post_smoke_composite_shader::set_samplers(
    int smoke_sampler,int depth_sampler)
{
    gl::set_uniform_integer(smoke_image,smoke_sampler);
    gl::set_uniform_integer(opaque_depth,depth_sampler);
}

jkgm::shaders::post_scale_shader::post_scale_shader(fs::path const &data_root)
    : opengl_shader("post_scale",
                    data_root / "shaders/postprocess.vert",
                    data_root / "shaders/post_scale.frag")
    , fbuf_image(gl::get_uniform_location(the_program, "fbuf_image"))
    , fused_bloom_first_pass(gl::get_uniform_location(
        the_program,"fused_bloom_first_pass"))
    , exposure_image(gl::get_uniform_location(the_program,"exposure_image"))
    , exposure_enabled(gl::get_uniform_location(the_program,"exposure_enabled"))
    , hdr_exposure_compensation(gl::get_uniform_location(
        the_program,"hdr_exposure_compensation"))
{
}

void jkgm::shaders::post_scale_shader::set_fbuf_sampler(int sampler)
{
    gl::set_uniform_integer(fbuf_image, sampler);
}

void jkgm::shaders::post_scale_shader::set_fused_bloom(
    bool first_pass,int exposure_sampler,bool adaptive,float compensation_ev)
{
    gl::set_uniform_integer(fused_bloom_first_pass,first_pass?1:0);
    gl::set_uniform_integer(exposure_image,exposure_sampler);
    gl::set_uniform_integer(exposure_enabled,adaptive?1:0);
    gl::set_uniform_float(hdr_exposure_compensation,compensation_ev);
}

jkgm::shaders::post_spatial_blur_shader::post_spatial_blur_shader(
    fs::path const &data_root,bool half_resolution)
    : opengl_shader(half_resolution
                        ? "post_spatial_blur_half_res" : "post_spatial_blur",
                    data_root / "shaders/postprocess.vert",
                    data_root / (half_resolution
                        ? "shaders/post_spatial_blur_half_res.frag"
                        : "shaders/post_spatial_blur.frag"))
    , source_image(gl::get_uniform_location(the_program, "source_image"))
    , depth_image(gl::get_uniform_location(the_program, "depth_image"))
    , blur_axis(gl::get_uniform_location(the_program, "blur_axis"))
    , screen_size(gl::get_uniform_location(the_program, "screen_size"))
    , distance_params(gl::get_uniform_location(the_program, "distance_params"))
    , peripheral_params(gl::get_uniform_location(the_program, "peripheral_params"))
    , original_image(gl::get_uniform_location(the_program,"original_image"))
    , output_size(gl::get_uniform_location(the_program,"output_size"))
    , restore_original(gl::get_uniform_location(the_program,"restore_original"))
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

void jkgm::shaders::post_spatial_blur_shader::set_fast_inputs(
    int original_sampler,size<2,float> output_dims,bool fullres_unblurred)
{
    gl::set_uniform_integer(original_image,original_sampler);
    gl::set_uniform_vector(output_size,output_dims);
    gl::set_uniform_integer(restore_original,fullres_unblurred?1:0);
}

void jkgm::shaders::post_spatial_blur_shader::set_distance_params(point<4, float> params)
{
    gl::set_uniform_vector(distance_params, params);
}

void jkgm::shaders::post_spatial_blur_shader::set_peripheral_params(point<4, float> params)
{
    gl::set_uniform_vector(peripheral_params, params);
}


jkgm::shaders::post_spatial_blur_reconstruct_shader::
    post_spatial_blur_reconstruct_shader(fs::path const &data_root)
    : opengl_shader("post_spatial_blur_reconstruct",
                    data_root / "shaders/postprocess.vert",
                    data_root / "shaders/post_spatial_blur_reconstruct.frag")
    , blur_image(gl::get_uniform_location(the_program,"blur_image"))
    , original_image(gl::get_uniform_location(the_program,"original_image"))
    , depth_image(gl::get_uniform_location(the_program,"depth_image"))
    , screen_size(gl::get_uniform_location(the_program,"screen_size"))
    , distance_params(gl::get_uniform_location(the_program,"distance_params"))
    , peripheral_params(gl::get_uniform_location(the_program,"peripheral_params"))
{
}

void jkgm::shaders::post_spatial_blur_reconstruct_shader::set_inputs(
    int blur_sampler,int original_sampler,int depth_sampler)
{
    gl::set_uniform_integer(blur_image,blur_sampler);
    gl::set_uniform_integer(original_image,original_sampler);
    gl::set_uniform_integer(depth_image,depth_sampler);
}

void jkgm::shaders::post_spatial_blur_reconstruct_shader::set_screen_size(
    size<2,float> dims)
{
    gl::set_uniform_vector(screen_size,dims);
}

void jkgm::shaders::post_spatial_blur_reconstruct_shader::set_distance_params(
    point<4,float> params)
{
    gl::set_uniform_vector(distance_params,params);
}

void jkgm::shaders::post_spatial_blur_reconstruct_shader::set_peripheral_params(
    point<4,float> params)
{
    gl::set_uniform_vector(peripheral_params,params);
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

jkgm::shaders::post_auto_exposure_shader::post_auto_exposure_shader(
    fs::path const &data_root)
    : opengl_shader("post_auto_exposure",
                    data_root / "shaders/postprocess.vert",
                    data_root / "shaders/post_auto_exposure.frag")
    , scene_image(gl::get_uniform_location(the_program,"scene_image"))
    , previous_exposure(gl::get_uniform_location(the_program,"previous_exposure"))
    , exposure_params(gl::get_uniform_location(the_program,"exposure_params"))
{
}

void jkgm::shaders::post_auto_exposure_shader::set_inputs(
    int scene_sampler,int history_sampler)
{
    gl::set_uniform_integer(scene_image,scene_sampler);
    gl::set_uniform_integer(previous_exposure,history_sampler);
}

void jkgm::shaders::post_auto_exposure_shader::set_params(point<4,float> params)
{
    gl::set_uniform_vector(exposure_params,params);
}

jkgm::shaders::post_to_srgb_shader::post_to_srgb_shader(fs::path const &data_root)
    : opengl_shader("post_to_srgb",
                    data_root / "shaders/postprocess.vert",
                    data_root / "shaders/post_to_srgb.frag")
    , fbuf_image(gl::get_uniform_location(the_program, "fbuf_image"))
    , grain_params(gl::get_uniform_location(the_program, "grain_params"))
    , vignette_params(gl::get_uniform_location(the_program, "vignette_params"))
    , rgb_split_pixels(gl::get_uniform_location(the_program, "rgb_split_pixels"))
    , brightness(gl::get_uniform_location(the_program, "brightness"))
    , contrast(gl::get_uniform_location(the_program, "contrast"))
    , saturation(gl::get_uniform_location(the_program, "saturation"))
    , exposure_image(gl::get_uniform_location(the_program, "exposure_image"))
    , exposure_enabled(gl::get_uniform_location(the_program, "exposure_enabled"))
    , hdr_enabled(gl::get_uniform_location(the_program, "hdr_enabled"))
    , hdr_exposure_compensation(gl::get_uniform_location(the_program, "hdr_exposure_compensation"))
    , hdr_shadow_softness(gl::get_uniform_location(the_program, "hdr_shadow_softness"))
    , hdr_color_crispness(gl::get_uniform_location(the_program, "hdr_color_crispness"))
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

void jkgm::shaders::post_to_srgb_shader::set_hdr(
    int sampler,bool hdr,bool adaptive,float compensation_ev,
    float shadow_softness,float color_crispness)
{
    gl::set_uniform_integer(exposure_image,sampler);
    gl::set_uniform_integer(exposure_enabled,adaptive?1:0);
    gl::set_uniform_integer(hdr_enabled,hdr?1:0);
    gl::set_uniform_float(hdr_exposure_compensation,compensation_ev);
    gl::set_uniform_float(hdr_shadow_softness,shadow_softness);
    gl::set_uniform_float(hdr_color_crispness,color_crispness);
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

void jkgm::shaders::post_to_srgb_shader::set_rgb_split(float pixels)
{
    gl::set_uniform_float(rgb_split_pixels,pixels);
}

void jkgm::shaders::post_to_srgb_shader::set_color_filter(float brightness_value,
                                                          float contrast_value,
                                                          float saturation_value)
{
    gl::set_uniform_float(brightness, brightness_value);
    gl::set_uniform_float(contrast, contrast_value);
    gl::set_uniform_float(saturation, saturation_value);
}

