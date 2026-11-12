#include "renderer_ao_ssao.hpp"
#include "base/log.hpp"
#include "math/utility.hpp"
#include "opengl_state.hpp"
#include "glad/gl.h"
#include <algorithm>
#include <random>

jkgm::ssao_occlusion_buffer::ssao_occlusion_buffer(size<2, int> dims,bool packed)
    : viewport(make_point(0, 0), dims)
{
    gl::bind_framebuffer(gl::framebuffer_bind_target::any, fbo);

    gl::bind_texture(gl::texture_bind_target::texture_2d, tex);
    gl::tex_image_2d(gl::texture_bind_target::texture_2d,
                     /*level*/ 0,
                     packed?gl::texture_internal_format::rgba16f:
                            gl::texture_internal_format::r16f,
                     dims,
                     packed?gl::texture_pixel_format::rgba:
                            gl::texture_pixel_format::red,
                     gl::texture_pixel_type::float32,
                     span<char const>(nullptr, 0U));
    gl::set_texture_max_level(gl::texture_bind_target::texture_2d, 0U);
    gl::set_texture_mag_filter(gl::texture_bind_target::texture_2d, gl::mag_filter::linear);
    gl::set_texture_min_filter(gl::texture_bind_target::texture_2d, gl::min_filter::linear);
    gl::set_texture_wrap_mode(gl::texture_bind_target::texture_2d,
                              gl::texture_direction::s,
                              gl::texture_wrap_mode::clamp_to_edge);
    gl::set_texture_wrap_mode(gl::texture_bind_target::texture_2d,
                              gl::texture_direction::t,
                              gl::texture_wrap_mode::clamp_to_edge);

    gl::framebuffer_texture(
        gl::framebuffer_bind_target::any, gl::framebuffer_attachment::color0, tex, 0);

    gl::draw_buffers(gl::draw_buffer::color0);

    auto fbs = gl::check_framebuffer_status(gl::framebuffer_bind_target::any);
    if(gl::check_framebuffer_status(gl::framebuffer_bind_target::any) !=
       gl::framebuffer_status::complete) {
        gl::log_errors();
        LOG_ERROR("Failed to create SSAO framebuffer: ", static_cast<int>(fbs));
    }

    gl::bind_framebuffer(gl::framebuffer_bind_target::any, gl::default_framebuffer);
}

jkgm::renderer_ao_ssao::renderer_ao_ssao(size<2, int> screen_res,bool half_res)
    : ssao_occlusionbuffer(screen_res)
    , ssao_occlusionbuffer2(screen_res)
    , half_resolution_requested(half_res)
{
    if(half_resolution_requested) {
        auto const half=make_size(
            std::max(1,(get<x>(screen_res)+1)/2),
            std::max(1,(get<y>(screen_res)+1)/2));
        half_raw=std::make_unique<ssao_occlusion_buffer>(half,true);
        half_blurred=std::make_unique<ssao_occlusion_buffer>(half,true);
    }
    std::uniform_real_distribution<float> ssao_noise_dist(0.0f, 1.0f);
    std::default_random_engine generator;

    ssao_kernel.reserve(16);
    for(size_t i = 0; i < 16; ++i) {
        float scale = (float)i / 16;
        scale *= scale;
        scale = lerp(0.1f, 1.0f, scale);
        ssao_kernel.push_back(normalize(point<3, float>(ssao_noise_dist(generator) * 2.0f - 1.0f,
                                                        ssao_noise_dist(generator) * 2.0f - 1.0f,
                                                        ssao_noise_dist(generator))) *
                              ssao_noise_dist(generator) * scale);
    }

    std::vector<point<2, float>> ssao_noise;
    ssao_noise.reserve(16);
    for(size_t i = 0; i < 16; ++i) {
        ssao_noise.push_back(normalize(make_point(ssao_noise_dist(generator) * 2.0f - 1.0f,
                                                  ssao_noise_dist(generator) * 2.0f - 1.0f)));
    }

    gl::bind_texture(gl::texture_bind_target::texture_2d, ssao_noise_texture);
    gl::tex_image_2d(gl::texture_bind_target::texture_2d,
                     /*level*/ 0,
                     gl::texture_internal_format::rg32f,
                     make_size(4, 4),
                     gl::texture_pixel_format::rg,
                     gl::texture_pixel_type::float32,
                     make_span(ssao_noise).as_const_bytes());
    gl::set_texture_max_level(gl::texture_bind_target::texture_2d, 0);
    gl::set_texture_min_filter(gl::texture_bind_target::texture_2d, gl::min_filter::nearest);
    gl::set_texture_mag_filter(gl::texture_bind_target::texture_2d, gl::mag_filter::nearest);
}

void jkgm::renderer_ao_ssao::update_ssao_texture(opengl_state *ogs,
                                                 gl::texture_view normal_tex,
                                                 gl::texture_view depth_tex)
{
    // The full-resolution G-buffer is authoritative. Each half-res pixel
    // chooses the nearest valid surface from its 2x2 source footprint.
    // Packed half-res AO keeps that surface's depth/normal for edge-aware
    // filtering without extra G-buffer lookups in the blur stage.
    bool const half=half_resolution_requested && half_raw && half_blurred &&
        ogs->game_post_ssao_half_program &&
        ogs->post_ssao_half_blur_program &&
        ogs->post_ssao_half_upsample_program &&
        glIsEnabled(GL_SCISSOR_TEST)==GL_FALSE;
    if(half) {
        gl::bind_vertex_array(ogs->postmdl.vao);
        gl::bind_framebuffer(gl::framebuffer_bind_target::any,half_raw->fbo);
        gl::set_viewport(half_raw->viewport);
        gl::clear({gl::clear_flag::color,gl::clear_flag::depth});
        auto &sample=*ogs->game_post_ssao_half_program;
        sample.use_program();
        sample.set_normal_sampler(0);
        sample.set_depth_sampler(1);
        sample.set_noise_sampler(2);
        sample.set_samples(make_span(ssao_kernel));
        gl::set_active_texture_unit(2);
        gl::bind_texture(gl::texture_bind_target::texture_2d,ssao_noise_texture);
        gl::set_active_texture_unit(1);
        gl::bind_texture(gl::texture_bind_target::texture_2d,depth_tex);
        gl::set_active_texture_unit(0);
        gl::bind_texture(gl::texture_bind_target::texture_2d,normal_tex);
        gl::draw_elements(gl::element_type::triangles,
                          ogs->postmdl.num_indices,gl::index_type::uint32);

        gl::bind_framebuffer(gl::framebuffer_bind_target::any,half_blurred->fbo);
        gl::set_viewport(half_blurred->viewport);
        gl::clear({gl::clear_flag::color,gl::clear_flag::depth});
        auto &blur=*ogs->post_ssao_half_blur_program;
        blur.use_program();
        blur.set_source_sampler(0);
        gl::set_active_texture_unit(0);
        gl::bind_texture(gl::texture_bind_target::texture_2d,half_raw->tex);
        gl::draw_elements(gl::element_type::triangles,
                          ogs->postmdl.num_indices,gl::index_type::uint32);

        // The existing opaque composite still consumes the identical
        // full-resolution single-channel AO texture.
        gl::bind_framebuffer(gl::framebuffer_bind_target::any,
                             ssao_occlusionbuffer.fbo);
        gl::set_viewport(ssao_occlusionbuffer.viewport);
        gl::clear({gl::clear_flag::color,gl::clear_flag::depth});
        auto &upsample=*ogs->post_ssao_half_upsample_program;
        upsample.use_program();
        upsample.set_source_sampler(0);
        upsample.set_normal_sampler(1);
        upsample.set_depth_sampler(2);
        gl::set_active_texture_unit(2);
        gl::bind_texture(gl::texture_bind_target::texture_2d,depth_tex);
        gl::set_active_texture_unit(1);
        gl::bind_texture(gl::texture_bind_target::texture_2d,normal_tex);
        gl::set_active_texture_unit(0);
        gl::bind_texture(gl::texture_bind_target::texture_2d,half_blurred->tex);
        gl::draw_elements(gl::element_type::triangles,
                          ogs->postmdl.num_indices,gl::index_type::uint32);
        return;
    }

    // Compute SSAO:
    gl::set_viewport(ssao_occlusionbuffer2.viewport);
    gl::bind_framebuffer(gl::framebuffer_bind_target::any, ssao_occlusionbuffer2.fbo);
    gl::clear({gl::clear_flag::color, gl::clear_flag::depth});

    ogs->game_post_ssao_program.use_program();
    ogs->game_post_ssao_program.set_normal_sampler(0);
    ogs->game_post_ssao_program.set_depth_sampler(1);
    ogs->game_post_ssao_program.set_noise_sampler(2);
    ogs->game_post_ssao_program.set_samples(make_span(ssao_kernel));

    gl::set_active_texture_unit(2);
    gl::bind_texture(gl::texture_bind_target::texture_2d, ssao_noise_texture);

    gl::set_active_texture_unit(1);
    gl::bind_texture(gl::texture_bind_target::texture_2d, depth_tex);

    gl::set_active_texture_unit(0);
    gl::bind_texture(gl::texture_bind_target::texture_2d, normal_tex);

    gl::bind_vertex_array(ogs->postmdl.vao);
    gl::draw_elements(
        gl::element_type::triangles, ogs->postmdl.num_indices, gl::index_type::uint32);

    // Blur SSAO:
    ogs->post_box4.use_program();
    ogs->post_box4.set_fbuf_sampler(0);
    ogs->post_box4.set_normal_sampler(1);
    ogs->post_box4.set_depth_sampler(2);

    gl::bind_framebuffer(gl::framebuffer_bind_target::any, ssao_occlusionbuffer.fbo);
    gl::set_viewport(ssao_occlusionbuffer.viewport);
    gl::clear({gl::clear_flag::color, gl::clear_flag::depth});

    gl::set_active_texture_unit(2);
    gl::bind_texture(gl::texture_bind_target::texture_2d, depth_tex);

    gl::set_active_texture_unit(1);
    gl::bind_texture(gl::texture_bind_target::texture_2d, normal_tex);

    gl::set_active_texture_unit(0);
    gl::bind_texture(gl::texture_bind_target::texture_2d, ssao_occlusionbuffer2.tex);

    gl::draw_elements(
        gl::element_type::triangles, ogs->postmdl.num_indices, gl::index_type::uint32);
}

jkgm::gl::texture_view jkgm::renderer_ao_ssao::get_ssao_texture() const
{
    return ssao_occlusionbuffer.tex;
}