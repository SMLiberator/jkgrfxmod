#pragma once
// Configurable-resolution R8 silhouette mask and streamed projected polygons.
// Overlapping triangles all write the same mask value, never multiply shade.
#include "sun_projected_geometry.hpp"
#include "base/log.hpp"
#include "glad/glad.h"
#include "glutil/buffer.hpp"
#include "glutil/framebuffer.hpp"
#include "glutil/texture.hpp"
#include "glutil/vertex_array.hpp"
#include <cstddef>

namespace jkgm {
struct projected_sun_mask_vertex {
    decal_vec3 world;
    decal_vec3 plane_normal;
};
static_assert(sizeof(projected_sun_mask_vertex)==sizeof(float)*6,
              "Projected sun vertex must contain two packed vec3s");
struct projected_sun_mask_gpu {
    gl::framebuffer fbo;
    gl::texture mask;
    gl::vertex_array vao;
    gl::buffer vbo;
    bool complete=false;
    projected_sun_mask_gpu(size<2,int> dimensions)
    {
        gl::set_active_texture_unit(0);
        gl::bind_texture(gl::texture_bind_target::texture_2d,mask);
        gl::tex_image_2d(gl::texture_bind_target::texture_2d,0,
            gl::texture_internal_format::r8,dimensions,
            gl::texture_pixel_format::red,gl::texture_pixel_type::uint8,
            span<char const>(nullptr,0U));
        gl::set_texture_max_level(gl::texture_bind_target::texture_2d,0);
        gl::set_texture_min_filter(gl::texture_bind_target::texture_2d,
                                   gl::min_filter::linear);
        gl::set_texture_mag_filter(gl::texture_bind_target::texture_2d,
                                   gl::mag_filter::linear);
        gl::bind_framebuffer(gl::framebuffer_bind_target::any,fbo);
        gl::framebuffer_texture(gl::framebuffer_bind_target::any,
                                gl::framebuffer_attachment::color0,mask,0);
        gl::draw_buffers(gl::draw_buffer::color0);
        complete=gl::check_framebuffer_status(
            gl::framebuffer_bind_target::any)==gl::framebuffer_status::complete;
        if(!complete)LOG_WARNING("Projected sun shadow mask FBO incomplete");
        gl::bind_framebuffer(gl::framebuffer_bind_target::any,
                             gl::default_framebuffer);
        gl::bind_vertex_array(vao);
        gl::bind_buffer(gl::buffer_bind_target::array,vbo);
        gl::enable_vertex_attrib_array(0);
        gl::vertex_attrib_pointer(0,3,gl::vertex_element_type::float32,
                                  false,sizeof(projected_sun_mask_vertex),
                                  offsetof(projected_sun_mask_vertex,world));
        gl::enable_vertex_attrib_array(1);
        gl::vertex_attrib_pointer(1,3,gl::vertex_element_type::float32,
                                  false,sizeof(projected_sun_mask_vertex),
                                  offsetof(projected_sun_mask_vertex,plane_normal));
        gl::bind_vertex_array(gl::vertex_array_view(0U));
    }
};
} // namespace jkgm
