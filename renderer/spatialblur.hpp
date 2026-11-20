#pragma once

#include "opengl_state.hpp"
#include "postfx.hpp"
#include "glad/gl.h"
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
        if(distance[2] <= 0.0001f && peripheral[2] <= 0.0001f) {
            return source;
        }

        // Standard dual half-resolution blur, with safe scissor fallback.
        bool const halfres=ogs->post_spatial_blur_fast &&
            ogs->spatial_blur_half_work && ogs->spatial_blur_half_result &&
            ogs->post_spatial_blur_reconstruct &&
            glIsEnabled(GL_SCISSOR_TEST)==GL_FALSE;
        auto &shader=halfres
            ?*ogs->post_spatial_blur_fast:ogs->post_spatial_blur;
        auto &work=halfres
            ?*ogs->spatial_blur_half_work:ogs->spatial_blur_work;
        gl::disable(gl::capability::depth_test);
        gl::disable(gl::capability::blend);
        gl::disable(gl::capability::cull_face);
        gl::set_depth_mask(false);
        gl::bind_vertex_array(ogs->postmdl.vao);

        shader.use_program();
        shader.set_source_sampler(0);
        shader.set_depth_sampler(1);
        shader.set_screen_size(static_cast<size<2, float>>(screen_size));
        auto const distance_params=make_point(
            std::max(0.0f,distance[0]),
            std::max(distance[1],distance[0]+0.001f),
            std::clamp(distance[2],0.0f,1.0f),0.0f);
        auto const peripheral_params=make_point(
            std::clamp(peripheral[0],0.0f,1.0f),
            std::max(peripheral[1],peripheral[0]+0.001f),
            std::clamp(peripheral[2],0.0f,1.0f),0.0f);
        shader.set_distance_params(distance_params);
        shader.set_peripheral_params(peripheral_params);

        gl::set_active_texture_unit(1);
        gl::bind_texture(gl::texture_bind_target::texture_2d, linear_depth);
        gl::set_active_texture_unit(0);
        gl::bind_texture(gl::texture_bind_target::texture_2d, source);

        // Horizontal pass: quarter-size target in the normal path.
        shader.set_blur_axis(0);
        if(halfres) {
            shader.set_output_size(make_size(
                float(std::max(1,get<x>(screen_size)/2)),
                float(std::max(1,get<y>(screen_size)/2))));
        }
        gl::bind_framebuffer(gl::framebuffer_bind_target::any,work.fbo);
        gl::set_viewport(work.viewport);
        gl::draw_elements(
            gl::element_type::triangles, ogs->postmdl.num_indices, gl::index_type::uint32);

        // Vertical pass: quarter-size target, then reconstruct.
        shader.set_blur_axis(1);
        if(halfres) {
            shader.set_output_size(make_size(
                float(std::max(1,get<x>(screen_size)/2)),
                float(std::max(1,get<y>(screen_size)/2))));
        }
        auto &vertical_target=halfres
            ?*ogs->spatial_blur_half_result:ogs->spatial_blur_result;
        gl::bind_framebuffer(gl::framebuffer_bind_target::any,
                             vertical_target.fbo);
        gl::set_viewport(vertical_target.viewport);
        gl::bind_texture(gl::texture_bind_target::texture_2d,work.tex);
        gl::draw_elements(
            gl::element_type::triangles, ogs->postmdl.num_indices, gl::index_type::uint32);

        if(halfres) {
            // Full-res, depth-guided four-tap reconstruction. No blur or
            // downsampling ever touches the original HDR source texture.
            auto &rebuild=*ogs->post_spatial_blur_reconstruct;
            rebuild.use_program();
            rebuild.set_inputs(0,2,1);
            rebuild.set_screen_size(static_cast<size<2,float>>(screen_size));
            rebuild.set_distance_params(distance_params);
            rebuild.set_peripheral_params(peripheral_params);
            // Only reconstruction needs the full-resolution original.
            gl::set_active_texture_unit(2);
            gl::bind_texture(gl::texture_bind_target::texture_2d,source);
            gl::bind_framebuffer(gl::framebuffer_bind_target::any,
                                 ogs->spatial_blur_result.fbo);
            gl::set_viewport(ogs->spatial_blur_result.viewport);
            gl::set_active_texture_unit(0);
            gl::bind_texture(gl::texture_bind_target::texture_2d,
                             ogs->spatial_blur_half_result->tex);
            gl::draw_elements(
                gl::element_type::triangles,ogs->postmdl.num_indices,
                gl::index_type::uint32);
        }

        gl::set_depth_mask(true);
        return ogs->spatial_blur_result.tex;
    }
}
