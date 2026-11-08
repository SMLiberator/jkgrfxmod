#pragma once
// EXPERIMENTAL prefiltered variance shadow moments for both sun cascades.
// Allocated only when explicitly enabled. Standard 24-bit depth maps remain.
#include "base/log.hpp"
#include "glad/glad.h"
#include "glutil/framebuffer.hpp"
#include "glutil/texture.hpp"
#include <array>
#include <algorithm>
namespace jkgm {
struct sun_shadow_moments_gpu {
    std::array<gl::framebuffer,2> horizontal_fbo,filtered_fbo;
    std::array<gl::texture,2> horizontal,filtered;
    std::array<unsigned,2> resolutions{};
    std::array<bool,2> valid{{false,false}};
    bool complete=true;

    explicit sun_shadow_moments_gpu(unsigned near_pixels,unsigned far_pixels)
    {
        resolutions={{std::max(64U,near_pixels),std::max(64U,far_pixels)}};
        auto setup=[&](gl::framebuffer &fbo,gl::texture &tex,unsigned pixels) {
            gl::set_active_texture_unit(0);
            gl::bind_texture(gl::texture_bind_target::texture_2d,tex);
            gl::tex_image_2d(gl::texture_bind_target::texture_2d,0,
                gl::texture_internal_format::rg32f,
                make_size(int(pixels),int(pixels)),
                gl::texture_pixel_format::rg,gl::texture_pixel_type::float32,
                span<char const>(nullptr,0U));
            gl::set_texture_max_level(gl::texture_bind_target::texture_2d,0);
            gl::set_texture_min_filter(gl::texture_bind_target::texture_2d,
                                       gl::min_filter::linear);
            gl::set_texture_mag_filter(gl::texture_bind_target::texture_2d,
                                       gl::mag_filter::linear);
            gl::set_texture_wrap_mode(gl::texture_bind_target::texture_2d,
                gl::texture_direction::s,gl::texture_wrap_mode::clamp_to_edge);
            gl::set_texture_wrap_mode(gl::texture_bind_target::texture_2d,
                gl::texture_direction::t,gl::texture_wrap_mode::clamp_to_edge);
            gl::bind_framebuffer(gl::framebuffer_bind_target::any,fbo);
            gl::framebuffer_texture(gl::framebuffer_bind_target::any,
                                    gl::framebuffer_attachment::color0,tex,0);
            gl::draw_buffers(gl::draw_buffer::color0);
            return gl::check_framebuffer_status(
                       gl::framebuffer_bind_target::any)==
                   gl::framebuffer_status::complete;
        };
        for(int c=0;c<2;++c) {
            complete=setup(horizontal_fbo[c],horizontal[c],resolutions[c])&&complete;
            complete=setup(filtered_fbo[c],filtered[c],resolutions[c])&&complete;
        }
        gl::bind_framebuffer(gl::framebuffer_bind_target::any,
                             gl::default_framebuffer);
        if(!complete)
            LOG_WARNING("Experimental VSM moment framebuffer incomplete");
    }
};
}
