#pragma once
// Experimental eight-Thing directional shadow atlas. Configurable tile size;
// the two world cascades remain independent.
#include "sun_shadow.hpp"
#include "sun_dynamic_shadow_layout.hpp"
#include "base/log.hpp"
#include "glad/glad.h"
#include "glutil/framebuffer.hpp"
#include "glutil/texture.hpp"
namespace jkgm {
constexpr int sun_dynamic_shadow_slots=8;
constexpr int sun_dynamic_shadow_inset=2;
constexpr int sun_dynamic_shadow_columns=4;
constexpr int sun_dynamic_shadow_rows=2;
struct sun_dynamic_shadow_gpu {
    gl::framebuffer fbo;
    gl::texture depth;
    bool complete=false;
    unsigned tile_resolution=256U;
    explicit sun_dynamic_shadow_gpu(unsigned pixels=256U)
    {
        tile_resolution=std::max(64U,std::min(pixels,1024U));
        gl::set_active_texture_unit(0);
        gl::bind_texture(gl::texture_bind_target::texture_2d,depth);
        gl::tex_image_2d(gl::texture_bind_target::texture_2d,0,
            gl::texture_internal_format::depth_component_24,
            make_size(sun_dynamic_shadow_columns*int(tile_resolution),
                      sun_dynamic_shadow_rows*int(tile_resolution)),
            gl::texture_pixel_format::depth_component,
            gl::texture_pixel_type::float32,span<char const>(nullptr,0U));
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
        complete=gl::check_framebuffer_status(gl::framebuffer_bind_target::any)
                 ==gl::framebuffer_status::complete;
        if(!complete)LOG_WARNING("Directional Thing shadow atlas incomplete");
        gl::bind_framebuffer(gl::framebuffer_bind_target::any,
                             gl::default_framebuffer);
    }
};
} // namespace jkgm
