#pragma once
// GPU storage for two camera-local directional sunlight shadow cascades.
// Hardware bilinear depth comparisons smooth edges without extra passes.
#include "sun_shadow.hpp"
#include "base/log.hpp"
#include "glad/glad.h"
#include "glutil/buffer.hpp"
#include "glutil/framebuffer.hpp"
#include "glutil/texture.hpp"
#include "glutil/vertex_array.hpp"
#include <algorithm>
#include <cstdint>

namespace jkgm {
struct sun_shadow_gpu {
    gl::framebuffer fbo,far_fbo;
    gl::texture depth,far_depth;
    gl::vertex_array vao;
    gl::buffer vertices,indices;
    decal_world_geometry world{};
    spotlight_shadow_static_mesh mesh{};
    std::vector<spotlight_shadow_surface_range> selected;
    unsigned resolution=512U;
    bool complete=false;
    uintptr_t loaded_world=0;
    uint64_t revision=0;

    explicit sun_shadow_gpu(unsigned pixels=512U)
    {
        resolution=std::max(64U,std::min(pixels,2048U));
        gl::set_active_texture_unit(0);
        gl::bind_texture(gl::texture_bind_target::texture_2d,depth);
        gl::tex_image_2d(gl::texture_bind_target::texture_2d,0,
            gl::texture_internal_format::depth_component_24,
            make_size(int(resolution),int(resolution)),
            gl::texture_pixel_format::depth_component,
            gl::texture_pixel_type::float32,
            span<char const>(nullptr,0U));
        gl::set_texture_max_level(gl::texture_bind_target::texture_2d,0);
        gl::set_texture_min_filter(gl::texture_bind_target::texture_2d,
                                   gl::min_filter::linear);
        gl::set_texture_mag_filter(gl::texture_bind_target::texture_2d,
                                   gl::mag_filter::linear);
        gl::set_texture_wrap_mode(gl::texture_bind_target::texture_2d,
            gl::texture_direction::s,gl::texture_wrap_mode::clamp_to_border);
        gl::set_texture_wrap_mode(gl::texture_bind_target::texture_2d,
            gl::texture_direction::t,gl::texture_wrap_mode::clamp_to_border);
        gl::set_texture_border_color(gl::texture_bind_target::texture_2d,
                                     color(1.0f,1.0f,1.0f,1.0f));
        gl::set_texture_compare_mode(gl::texture_bind_target::texture_2d,
                                     gl::texture_compare_mode::compare_ref_to_texture);
        gl::set_texture_compare_function(gl::texture_bind_target::texture_2d,
                                         gl::comparison_function::less_equal);
        gl::bind_framebuffer(gl::framebuffer_bind_target::any,fbo);
        gl::framebuffer_texture(gl::framebuffer_bind_target::any,
                                gl::framebuffer_attachment::depth,depth,0);
        gl::draw_buffers(gl::draw_buffer::none);
        glReadBuffer(GL_NONE);
        complete=gl::check_framebuffer_status(
                     gl::framebuffer_bind_target::any)==
                     gl::framebuffer_status::complete;
        if(!complete)LOG_WARNING("Sun shadow depth framebuffer incomplete");
        gl::bind_framebuffer(gl::framebuffer_bind_target::any,
                             gl::default_framebuffer);
        // The second cascade shares the exact same world mesh/VAO.
        // Only a second depth attachment is needed.
        gl::bind_texture(gl::texture_bind_target::texture_2d,far_depth);
        gl::tex_image_2d(gl::texture_bind_target::texture_2d,0,
            gl::texture_internal_format::depth_component_24,
            make_size(int(resolution),int(resolution)),
            gl::texture_pixel_format::depth_component,
            gl::texture_pixel_type::float32,
            span<char const>(nullptr,0U));
        gl::set_texture_max_level(gl::texture_bind_target::texture_2d,0);
        gl::set_texture_min_filter(gl::texture_bind_target::texture_2d,
                                   gl::min_filter::linear);
        gl::set_texture_mag_filter(gl::texture_bind_target::texture_2d,
                                   gl::mag_filter::linear);
        gl::set_texture_wrap_mode(gl::texture_bind_target::texture_2d,
            gl::texture_direction::s,gl::texture_wrap_mode::clamp_to_border);
        gl::set_texture_wrap_mode(gl::texture_bind_target::texture_2d,
            gl::texture_direction::t,gl::texture_wrap_mode::clamp_to_border);
        gl::set_texture_border_color(gl::texture_bind_target::texture_2d,
                                     color(1.0f,1.0f,1.0f,1.0f));
        gl::set_texture_compare_mode(gl::texture_bind_target::texture_2d,
                                     gl::texture_compare_mode::compare_ref_to_texture);
        gl::set_texture_compare_function(gl::texture_bind_target::texture_2d,
                                         gl::comparison_function::less_equal);
        gl::bind_framebuffer(gl::framebuffer_bind_target::any,far_fbo);
        gl::framebuffer_texture(gl::framebuffer_bind_target::any,
                                gl::framebuffer_attachment::depth,far_depth,0);
        gl::draw_buffers(gl::draw_buffer::none);
        glReadBuffer(GL_NONE);
        bool const far_complete=gl::check_framebuffer_status(
            gl::framebuffer_bind_target::any)==
            gl::framebuffer_status::complete;
        if(!far_complete)LOG_WARNING("Far sun shadow framebuffer incomplete");
        complete=complete && far_complete;
        gl::bind_framebuffer(gl::framebuffer_bind_target::any,
                             gl::default_framebuffer);
        gl::bind_vertex_array(vao);
        gl::bind_buffer(gl::buffer_bind_target::array,vertices);
        gl::enable_vertex_attrib_array(0);
        gl::vertex_attrib_pointer(0,3,gl::vertex_element_type::float32,
                                  false,sizeof(spotlight_shadow_vertex),
                                  offsetof(spotlight_shadow_vertex,position));
        gl::enable_vertex_attrib_array(1);
        gl::vertex_attrib_pointer(1,2,gl::vertex_element_type::float32,
                                  false,sizeof(spotlight_shadow_vertex),
                                  offsetof(spotlight_shadow_vertex,uv));
        gl::bind_buffer(gl::buffer_bind_target::element_array,indices);
        gl::bind_vertex_array(gl::vertex_array_view(0U));
    }

    bool update_world(decal_native_read const &read,uintptr_t address)
    {
        if(!complete || !address)return false;
        if(loaded_world==address && world.matches(read,address))return true;
        loaded_world=0;
        world.clear();
        mesh={};
        if(!world.load(read,address,250000U,true) ||
           !build_spotlight_shadow_static_mesh(world,mesh)) {
            world.clear();mesh={};return false;
        }
        gl::bind_vertex_array(vao);
        gl::bind_buffer(gl::buffer_bind_target::array,vertices);
        gl::buffer_data(gl::buffer_bind_target::array,
                        make_span(mesh.vertices).as_const_bytes(),
                        gl::buffer_usage::static_draw);
        gl::bind_buffer(gl::buffer_bind_target::element_array,indices);
        gl::buffer_data(gl::buffer_bind_target::element_array,
                        make_span(mesh.indices).as_const_bytes(),
                        gl::buffer_usage::static_draw);
        gl::bind_vertex_array(gl::vertex_array_view(0U));
        loaded_world=address;
        ++revision;
        LOG_INFO("Sun shadow static world ready surfaces=",mesh.surfaces.size(),
                 " indices=",mesh.indices.size());
        return true;
    }
};
} // namespace jkgm
