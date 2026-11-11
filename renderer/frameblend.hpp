#pragma once

#include "opengl_state.hpp"
#include "postfx.hpp"
#include <algorithm>

namespace jkgm {
    // A zero-strength trail requires neither a history write nor the
    // final fullscreen history copy. Retro must still stage a separate scene
    // so that both HUD layers are quantized only in the final pass.
    inline bool frame_blend_can_present_directly(opengl_state const *ogs)
    {
        if(ogs->retro_mode_active)return false;
        if(!ogs->enable_trails)return true;
        auto const &blend=get_frame_blend_settings().strength;
        // Consider the fade target as well as its current value. A fade
        // starting at zero must enter the history path immediately so it can
        // seed a clean frame before nonzero blending becomes visible.
        return blend.value()[0]<=0.0f && blend.target()[0]<=0.0f;
    }

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

        // Present the accumulated SCENE, without Retro quantization.
        // Retro stages this linear result in a separate framebuffer so
        // Camera-type HUDs and the DirectDraw HUD can be composited first.
        // The final CMP pass happens in renderer::end_frame() AFTER both HUDs.
        // The non-Retro path keeps its existing direct presentation.
        if(ogs->retro_mode_active && ogs->retro_present_buffer)
            gl::bind_framebuffer(gl::framebuffer_bind_target::any,
                                 ogs->retro_present_buffer->fbo);
        else
            gl::bind_framebuffer(gl::framebuffer_bind_target::any,
                                 gl::default_framebuffer);
        gl::set_viewport(make_box(make_point(0, 0), screen_size));
        ogs->post_frame_blend.set_blend_strength(0.0f);
        ogs->post_frame_blend.set_retro_palette(2,false);
        gl::set_active_texture_unit(0);
        gl::bind_texture(gl::texture_bind_target::texture_2d, next.tex);
        gl::draw_elements(
            gl::element_type::triangles, ogs->postmdl.num_indices, gl::index_type::uint32);

        ogs->frame_blend_history_valid = true;
        ogs->frame_blend_use_a = !ogs->frame_blend_use_a;
        gl::set_depth_mask(true);
    }
}
