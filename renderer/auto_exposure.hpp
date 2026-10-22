#pragma once
// Shared bloom/basic compositor GPU exposure pass. This shader renders
// precisely one RGBA16F pixel and retains the previous EV in a ping-pong FBO.
#include "opengl_state.hpp"
#include <algorithm>
#include <chrono>

namespace jkgm {

inline bool auto_exposure_enabled(opengl_state const *ogs)
{
    return ogs && ogs->enable_post && ogs->enable_auto_exposure &&
           ogs->auto_exposure_scene && ogs->auto_exposure_sensitivity>0.0f;
}

inline void update_auto_exposure(opengl_state *ogs,gl::texture_view scene)
{
    if(!auto_exposure_enabled(ogs)) {
        ogs->auto_exposure_history_valid=false;
        ogs->auto_exposure_last_frame={};
        return;
    }

    auto const now=std::chrono::steady_clock::now();
    float dt=0.0f;
    if(ogs->auto_exposure_history_valid) {
        dt=std::clamp(std::chrono::duration<float>(
            now-ogs->auto_exposure_last_frame).count(),0.0f,0.25f);
    }
    ogs->auto_exposure_last_frame=now;

    auto &dst=ogs->auto_exposure_next_a
        ?ogs->auto_exposure_a:ogs->auto_exposure_b;
    auto &src=ogs->auto_exposure_next_a
        ?ogs->auto_exposure_b:ogs->auto_exposure_a;
    gl::bind_framebuffer(gl::framebuffer_bind_target::any,dst.fbo);
    gl::set_viewport(dst.viewport);
    gl::disable(gl::capability::depth_test);
    gl::disable(gl::capability::cull_face);
    gl::disable(gl::capability::blend);
    gl::bind_vertex_array(ogs->postmdl.vao);
    ogs->post_auto_exposure.use_program();
    ogs->post_auto_exposure.set_inputs(0,1);
    ogs->post_auto_exposure.set_params(make_point(
        ogs->auto_exposure_sensitivity,dt,
        ogs->auto_exposure_history_valid?1.0f:0.0f,0.0f));
    gl::set_active_texture_unit(0);
    gl::bind_texture(gl::texture_bind_target::texture_2d,scene);
    gl::set_active_texture_unit(1);
    gl::bind_texture(gl::texture_bind_target::texture_2d,src.tex);
    gl::draw_elements(
        gl::element_type::triangles,ogs->postmdl.num_indices,
        gl::index_type::uint32);
    ogs->auto_exposure_history_valid=true;
    ogs->auto_exposure_next_a=!ogs->auto_exposure_next_a;
    gl::set_active_texture_unit(0);
}

// Called after the presentation shader has been made current.
inline void bind_auto_exposure(opengl_state *ogs)
{
    bool const enabled=auto_exposure_enabled(ogs) &&
                       ogs->auto_exposure_history_valid;
    ogs->post_to_srgb.set_auto_exposure(6,enabled);
    gl::set_active_texture_unit(6);
    gl::bind_texture(gl::texture_bind_target::texture_2d,
        enabled
        ? (ogs->auto_exposure_next_a
            ? gl::texture_view(ogs->auto_exposure_b.tex)
            : gl::texture_view(ogs->auto_exposure_a.tex))
        : gl::default_texture);
    gl::set_active_texture_unit(0);
}

} // namespace jkgm
