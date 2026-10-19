#pragma once

#include "opengl_state.hpp"
#include "postfx.hpp"
#include <algorithm>

namespace jkgm {
    // Called after post_to_srgb has drawn the completed presentation frame
    // into screen_postbuffer1. All operations here work in linear light.
    inline void present_frame_blend(opengl_state *ogs, size<2, int> screen_size)
    {
        auto &settings = get_frame_blend_settings();
        if(ogs->frame_blend_generation != settings.reset_generation) {
            // ResetFrameBlend and menu/game transitions drop old history.
            ogs->frame_blend_history_valid = false;
            ogs->frame_blend_generation = settings.reset_generation;
        }

        auto &previous = ogs->frame_blend_use_a ? ogs->frame_blend_history_a
                                               : ogs->frame_blend_history_b;
        auto &next = ogs->frame_blend_use_a ? ogs->frame_blend_history_b
                                           : ogs->frame_blend_history_a;

        float strength = std::clamp(settings.strength.value()[0], 0.0f, 1.0f);
        if(!ogs->enable_trails || !ogs->frame_blend_history_valid) {
            strength = 0.0f;
        }

        gl::disable(gl::capability::blend);
        gl::disable(gl::capability::depth_test);
        gl::disable(gl::capability::cull_face);
        gl::set_depth_mask(false);
        gl::bind_vertex_array(ogs->postmdl.vao);

        // Read the new frame and the PREVIOUS history while writing a
        // DIFFERENT history texture, avoiding GL feedback loops.
        gl::bind_framebuffer(gl::framebuffer_bind_target::any, next.fbo);
        gl::set_viewport(next.viewport);
        ogs->post_frame_blend.use_program();
        ogs->post_frame_blend.set_current_sampler(0);
        ogs->post_frame_blend.set_history_sampler(1);
        ogs->post_frame_blend.set_blend_strength(strength);
        ogs->post_frame_blend.set_retro_palette(2,false);

        gl::set_active_texture_unit(1);
        gl::bind_texture(gl::texture_bind_target::texture_2d, previous.tex);
        gl::set_active_texture_unit(0);
        gl::bind_texture(gl::texture_bind_target::texture_2d, ogs->screen_postbuffer1.tex);

        gl::draw_elements(
            gl::element_type::triangles, ogs->postmdl.num_indices, gl::index_type::uint32);

        // Present the completed accumulated image. This is the only pass that
        // renders into the default framebuffer, where GL_FRAMEBUFFER_SRGB
        // converts the linear fragment output to sRGB as before.
        gl::bind_framebuffer(gl::framebuffer_bind_target::any, gl::default_framebuffer);
        gl::set_viewport(make_box(make_point(0, 0), screen_size));
        ogs->post_frame_blend.set_blend_strength(0.0f);
        ogs->post_frame_blend.set_retro_palette(2,ogs->retro_mode_active);
        if(ogs->retro_mode_active) {
            gl::set_active_texture_unit(2);
            gl::bind_texture(gl::texture_bind_target::texture_2d,ogs->retro_lut_texture);
            gl::set_active_texture_unit(0);
        }
        gl::bind_texture(gl::texture_bind_target::texture_2d, next.tex);
        gl::draw_elements(
            gl::element_type::triangles, ogs->postmdl.num_indices, gl::index_type::uint32);

        ogs->frame_blend_history_valid = true;
        ogs->frame_blend_use_a = !ogs->frame_blend_use_a;
        gl::set_depth_mask(true);
    }
}
