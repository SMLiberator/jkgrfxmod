#include "renderer_compositor_bloom.hpp"
#include "math/colors.hpp"
#include "opengl_state.hpp"
#include "frameblend.hpp"
#include "postfx.hpp"
#include "auto_exposure.hpp"
#include "glad/gl.h"

void jkgm::renderer_compositor_bloom::end_frame(opengl_state *ogs,
                                                size<2, int> screen_size,
                                                gl::texture_view composed_screen)
{
    update_auto_exposure(ogs,composed_screen);

    // Scissored fullscreen draws cannot fully overwrite intermediate FBOs.
    // Preserve the legacy sequence whenever scissor testing is enabled.
    bool const full_viewport=glIsEnabled(GL_SCISSOR_TEST)==GL_FALSE;
    bool const fused_bloom=ogs->experimental_fused_bloom_downsample &&
                           full_viewport;
    bool const skip_bloom_clears=ogs->experimental_skip_bloom_clears &&
                                 full_viewport;
    if(fused_bloom || skip_bloom_clears) {
        // A skipped clear is safe only if each pass overwrites ALL pixels.
        // In particular, retained GL blending would otherwise accumulate
        // previous-frame bloom data into the temporary FBOs.
        gl::disable(gl::capability::blend);
        gl::disable(gl::capability::depth_test);
    }
    gl::bind_vertex_array(ogs->postmdl.vao);
    if(!fused_bloom) {
        // Original full-resolution bloom extraction, unchanged by default.
        gl::bind_framebuffer(gl::framebuffer_bind_target::any,
                             ogs->screen_postbuffer2.fbo);
        gl::set_viewport(make_box(make_point(0,0),screen_size));
        if(!skip_bloom_clears) {
            gl::set_clear_color(solid(colors::black));
            gl::clear({gl::clear_flag::color,gl::clear_flag::depth});
        }
        ogs->post_low_pass.use_program();
        ogs->post_low_pass.set_fbuf_sampler(0);
        bind_bloom_exposure(ogs);
        gl::set_active_texture_unit(0);
        gl::bind_texture(gl::texture_bind_target::texture_2d,composed_screen);
        gl::draw_elements(gl::element_type::triangles,
                          ogs->postmdl.num_indices,gl::index_type::uint32);
    }

    // Opt-in: threshold source pixels inside the first downsample shader.
    // This avoids writing and reading the full-resolution bright-pass FBO.
    gl::texture_view src_tx=fused_bloom
        ?composed_screen:gl::texture_view(ogs->screen_postbuffer2.tex);
    bool const adaptive=auto_exposure_enabled(ogs) &&
                        ogs->auto_exposure_history_valid;
    if(fused_bloom) {
        gl::set_active_texture_unit(1);
        gl::bind_texture(gl::texture_bind_target::texture_2d,
            adaptive
                ?(ogs->auto_exposure_next_a
                    ?gl::texture_view(ogs->auto_exposure_b.tex)
                    :gl::texture_view(ogs->auto_exposure_a.tex))
                :gl::default_texture);
        gl::set_active_texture_unit(0);
    }
    ogs->post_scale.use_program();
    ogs->post_scale.set_fbuf_sampler(0);
    unsigned level=0U;
    for(auto &hdr_stack_em : ogs->bloom_layers.elements) {
        ogs->post_scale.set_fused_bloom(fused_bloom && level==0U,1,
            adaptive,hdr_enabled(ogs)?ogs->hdr_exposure_compensation:0.0f);
        gl::bind_framebuffer(gl::framebuffer_bind_target::any,
                             hdr_stack_em.a.fbo);
        gl::set_viewport(hdr_stack_em.a.viewport);
        if(!skip_bloom_clears) {
            gl::set_clear_color(solid(colors::black));
            gl::clear({gl::clear_flag::color,gl::clear_flag::depth});
        }
        gl::bind_texture(gl::texture_bind_target::texture_2d,src_tx);
        gl::draw_elements(gl::element_type::triangles,
                          ogs->postmdl.num_indices,gl::index_type::uint32);
        src_tx=hdr_stack_em.a.tex;
        ++level;
    }

    // Blur:
    gl::set_active_texture_unit(0);

    for(auto &hdr_stack_em : ogs->bloom_layers.elements) {
        shaders::post_gauss_shader *curr_prog = &ogs->post_gauss3;
        switch(hdr_stack_em.taps) {
        case 15:
            curr_prog = &ogs->post_gauss15;
            break;

        case 9:
            curr_prog = &ogs->post_gauss9;
            break;

        case 7:
            curr_prog = &ogs->post_gauss7;
            break;

        default:
        case 3:
            curr_prog = &ogs->post_gauss3;
            break;
        }

        curr_prog->use_program();
        curr_prog->set_fbuf_sampler(0);

        // Blur horizontally
        gl::bind_framebuffer(gl::framebuffer_bind_target::any, hdr_stack_em.b.fbo);
        gl::set_viewport(hdr_stack_em.b.viewport);

        if(!skip_bloom_clears) {
            gl::set_clear_color(solid(colors::black));
            gl::clear({gl::clear_flag::color,gl::clear_flag::depth});
        }
        curr_prog->set_blur_direction(make_direction(1, 0));
        gl::bind_texture(gl::texture_bind_target::texture_2d, hdr_stack_em.a.tex);
        gl::draw_elements(
            gl::element_type::triangles, ogs->postmdl.num_indices, gl::index_type::uint32);

        // Blur vertically
        gl::bind_framebuffer(gl::framebuffer_bind_target::any, hdr_stack_em.a.fbo);

        if(!skip_bloom_clears) {
            gl::set_clear_color(solid(colors::black));
            gl::clear({gl::clear_flag::color,gl::clear_flag::depth});
        }
        curr_prog->set_blur_direction(make_direction(0, 1));
        gl::bind_texture(gl::texture_bind_target::texture_2d, hdr_stack_em.b.tex);
        gl::draw_elements(
            gl::element_type::triangles, ogs->postmdl.num_indices, gl::index_type::uint32);
    }

    // The inactive-trail fast path writes directly to the window, skipping
    // both full-resolution history passes. A future trail fade enters the
    // history path on its first frame; Retro always retains its HUD staging.
    bool const direct_present=frame_blend_can_present_directly(ogs);
    if(direct_present) {
        gl::bind_framebuffer(gl::framebuffer_bind_target::any,
                             gl::default_framebuffer);
        ogs->frame_blend_history_valid=false;
    }
    else {
        gl::bind_framebuffer(gl::framebuffer_bind_target::any,
                             ogs->screen_postbuffer1.fbo);
    }
    gl::set_viewport(make_box(make_point(0, 0), screen_size));

    gl::set_clear_color(solid(colors::black));
    gl::clear({gl::clear_flag::color, gl::clear_flag::depth});
    gl::disable(gl::capability::depth_test);
    gl::disable(gl::capability::cull_face);
    gl::disable(gl::capability::blend);

    // Render the finished post-processed presentation (including bloom and
    // optional effects) into linear HDR staging. Frame blending/presentation
    // happens in the shared final pass after this shader completes.
    ogs->post_to_srgb.use_program();
    ogs->post_to_srgb.set_fbuf_sampler(0);
    bind_auto_exposure(ogs);

    gl::set_active_texture_unit(0);
    gl::bind_texture(gl::texture_bind_target::texture_2d, composed_screen);

    std::array<int, shaders::post_to_srgb_shader::num_bloom_layers> bloom_samplers;
    std::array<float, shaders::post_to_srgb_shader::num_bloom_layers> bloom_weights;

    int curr_sampler = 1;
    for(int i = 0; i < shaders::post_to_srgb_shader::num_bloom_layers; ++i) {
        auto const &hdr_stack_em = ogs->bloom_layers.elements.at(i);

        bloom_samplers.at(i) = curr_sampler;
        gl::set_active_texture_unit(curr_sampler);
        gl::bind_texture(gl::texture_bind_target::texture_2d, hdr_stack_em.a.tex);
        ++curr_sampler;

        bloom_weights.at(i) = hdr_stack_em.weight;
    }

    ogs->post_to_srgb.set_bloom_samplers(make_span(bloom_samplers));
    ogs->post_to_srgb.set_bloom_weights(make_span(bloom_weights));
    apply_post_fx(ogs->post_to_srgb, ogs->enable_post);

    gl::draw_elements(
        gl::element_type::triangles, ogs->postmdl.num_indices, gl::index_type::uint32);

    if(direct_present) {
        // Match present_frame_blend's GL depth state for the next frame.
        gl::set_depth_mask(true);
    }
    else {
        present_frame_blend(ogs, screen_size);
    }
}