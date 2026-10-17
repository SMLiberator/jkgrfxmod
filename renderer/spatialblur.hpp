#pragma once

#include "opengl_state.hpp"
#include "postfx.hpp"
#include <algorithm>

namespace jkgm {
    // Spatial blur is intentionally separate from recursive frame blending.
    // Applied to the resolved game frame before bloom and color grading.
    inline gl::texture_view apply_spatial_blur(
        opengl_state *ogs, size<2, int> screen_size,
        gl::texture_view source, gl::texture_view linear_depth)
    {
        if(!ogs->enable_blur) {
            return source;
        }

        auto distance = get_distance_blur_settings().params.value();
        auto peripheral = get_peripheral_blur_settings().params.value();
        if(!ogs->enable_distance_blur) {
            distance[2] = 0.0f;
        }
        if(!ogs->enable_peripheral_blur) {
            peripheral[2] = 0.0f;
        }
        if(distance[2] <= 0.0001f && peripheral[2] <= 0.0001f) {
            return source;
        }

        auto &shader = ogs->post_spatial_blur;
        gl::disable(gl::capability::depth_test);
        gl::disable(gl::capability::blend);
        gl::disable(gl::capability::cull_face);
        gl::set_depth_mask(false);
        gl::bind_vertex_array(ogs->postmdl.vao);

        shader.use_program();
        shader.set_source_sampler(0);
        shader.set_depth_sampler(1);
        shader.set_screen_size(static_cast<size<2, float>>(screen_size));
        shader.set_distance_params(
            make_point(std::max(0.0f, distance[0]),
                       std::max(distance[1], distance[0] + 0.001f),
                       std::clamp(distance[2], 0.0f, 1.0f), 0.0f));
        shader.set_peripheral_params(
            make_point(std::clamp(peripheral[0], 0.0f, 1.0f),
                       std::max(peripheral[1], peripheral[0] + 0.001f),
                       std::clamp(peripheral[2], 0.0f, 1.0f), 0.0f));

        gl::set_active_texture_unit(1);
        gl::bind_texture(gl::texture_bind_target::texture_2d, linear_depth);
        gl::set_active_texture_unit(0);
        gl::bind_texture(gl::texture_bind_target::texture_2d, source);

        // Horizontal pass
        shader.set_blur_axis(0);
        gl::bind_framebuffer(gl::framebuffer_bind_target::any,
                             ogs->spatial_blur_work.fbo);
        gl::set_viewport(ogs->spatial_blur_work.viewport);
        gl::draw_elements(
            gl::element_type::triangles, ogs->postmdl.num_indices, gl::index_type::uint32);

        // Vertical pass. Source and destination are always distinct.
        shader.set_blur_axis(1);
        gl::bind_framebuffer(gl::framebuffer_bind_target::any,
                             ogs->spatial_blur_result.fbo);
        gl::set_viewport(ogs->spatial_blur_result.viewport);
        gl::bind_texture(gl::texture_bind_target::texture_2d, ogs->spatial_blur_work.tex);
        gl::draw_elements(
            gl::element_type::triangles, ogs->postmdl.num_indices, gl::index_type::uint32);

        gl::set_depth_mask(true);
        return ogs->spatial_blur_result.tex;
    }
}
