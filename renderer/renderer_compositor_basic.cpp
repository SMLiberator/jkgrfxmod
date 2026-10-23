#include "renderer_compositor_basic.hpp"
#include "math/colors.hpp"
#include "opengl_state.hpp"
#include "frameblend.hpp"
#include "postfx.hpp"
#include "auto_exposure.hpp"

void jkgm::renderer_compositor_basic::end_frame(opengl_state *ogs,
                                                size<2, int> screen_size,
                                                gl::texture_view composed_screen)
{
    update_auto_exposure(ogs,composed_screen);

    // Compose renderbuffer onto window:
    gl::bind_vertex_array(ogs->postmdl.vao);

    // Without trails or Retro quantization, the completed linear frame can
    // be drawn straight to the sRGB window. Avoid the otherwise redundant
    // full-resolution history write and subsequent copy to the backbuffer.
    bool const direct_present=!ogs->enable_trails && !ogs->retro_mode_active;
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
        gl::bind_texture(gl::texture_bind_target::texture_2d, gl::default_texture);
        ++curr_sampler;

        // Bloom is disabled in the basic compositor. Keep the shader's
        // weights zero so it can skip all five texture samples entirely.
        bloom_weights.at(i) = 0.0f;
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