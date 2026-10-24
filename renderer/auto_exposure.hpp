#pragma once
// Shared bloom/basic compositor GPU exposure pass. This shader renders
// precisely one RGBA16F pixel and retains the previous EV in a ping-pong FBO.
#include "opengl_state.hpp"
#include "base/log.hpp"
#include "glad/gl.h"
#include <algorithm>
#include <chrono>
#include <sstream>

namespace jkgm {

inline bool hdr_enabled(opengl_state const *ogs)
{
    return ogs && ogs->enable_post && ogs->auto_exposure_scene &&
           (ogs->enable_hdr || ogs->auto_exposure_sensitivity>0.0f);
}

inline bool auto_exposure_enabled(opengl_state const *ogs)
{
    return hdr_enabled(ogs) && ogs->auto_exposure_sensitivity>0.0f;
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
    // Scissor state inherited from native rendering must never clip the
    // single output pixel and silently freeze the temporal exposure buffer.
    GLboolean const had_scissor=glIsEnabled(GL_SCISSOR_TEST);
    if(had_scissor) glDisable(GL_SCISSOR_TEST);
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
    if(had_scissor) glEnable(GL_SCISSOR_TEST);

    // Debug once every 2s only with profile_auto_exposure=true. A single-pixel
    // readback is intentionally not in the normal path: it synchronizes GL.
    if(ogs->auto_exposure_diagnostics &&
       (ogs->auto_exposure_last_log==std::chrono::steady_clock::time_point{} ||
        now-ogs->auto_exposure_last_log>=std::chrono::seconds(2))) {
        ogs->auto_exposure_last_log=now;
        float diagnostic[4]={0.0f,0.0f,0.0f,0.0f};
        glReadBuffer(GL_COLOR_ATTACHMENT0);
        glReadPixels(0,0,1,1,GL_RGBA,GL_FLOAT,diagnostic);
        std::ostringstream message;
        message << "Auto exposure: adapted_ev=" << diagnostic[0]
                << " target_ev=" << diagnostic[1]
                << " luminance=" << diagnostic[2]
                << " dt=" << dt
                << " sensitivity=" << ogs->auto_exposure_sensitivity
                << " compensation_ev=" << ogs->hdr_exposure_compensation;
        LOG_INFO(message.str());
    }
    ogs->auto_exposure_history_valid=true;
    ogs->auto_exposure_next_a=!ogs->auto_exposure_next_a;
    gl::set_active_texture_unit(0);
}

// Called after the presentation shader has been made current.
inline void bind_auto_exposure(opengl_state *ogs)
{
    bool const enabled=auto_exposure_enabled(ogs) &&
                       ogs->auto_exposure_history_valid;
    ogs->post_to_srgb.set_hdr(6,hdr_enabled(ogs),enabled,
                                ogs->hdr_exposure_compensation,
                                ogs->hdr_shadow_softness);
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
