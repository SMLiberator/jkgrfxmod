#include "gl.hpp"
#include "base/log.hpp"
#include "glad/gl.h"
#include <array>
#include <cstddef>
#include <cstdint>


namespace {
    // Per-thread cache: JKGM submits GL from the render thread. Invalidate
    // on frame/context transitions and before/after external raw state edits.
    struct cached_texture_unit {
        uint64_t generation=0;
        GLuint tex2d=~GLuint(0),texarray=~GLuint(0),texcube=~GLuint(0);
        GLuint texbuffer=~GLuint(0),texmsaa=~GLuint(0);
    };
    struct cached_submission_state {
        static constexpr size_t max_units=64U;
        std::array<cached_texture_unit,max_units> textures{};
        uint64_t generation=1U;
        int active_unit=-1;
        GLuint vao=~GLuint(0);
        GLenum winding=0,cull_mode=0;
        int cull_enabled=-1;
        jkgm::gl::state_cache_stats stats{};
    };
    thread_local cached_submission_state submission_state;

    GLuint *binding_slot(cached_texture_unit &unit,GLenum target) {
        switch(target) {
        case GL_TEXTURE_2D:return &unit.tex2d;
        case GL_TEXTURE_2D_ARRAY:return &unit.texarray;
        case GL_TEXTURE_CUBE_MAP:return &unit.texcube;
        case GL_TEXTURE_BUFFER:return &unit.texbuffer;
        case GL_TEXTURE_2D_MULTISAMPLE:return &unit.texmsaa;
        default:return nullptr;
        }
    }
}

void jkgm::gl::invalidate_cached_state() {
    auto &cache=submission_state;
    cache.active_unit=-1;
    cache.vao=~GLuint(0);
    cache.winding=0;
    cache.cull_mode=0;
    cache.cull_enabled=-1;
    if(++cache.generation==0U) {
        for(auto &item:cache.textures)item.generation=0;
        cache.generation=1U;
    }
}
jkgm::gl::state_cache_stats jkgm::gl::consume_state_cache_stats() {
    auto const value=submission_state.stats;
    submission_state.stats={};
    return value;
}
void jkgm::gl::set_active_texture_unit_raw(int unit) {
    auto &cache=submission_state;
    if(unit>=0 && cache.active_unit==unit) {
        ++cache.stats.active_skipped;
        return;
    }
    glActiveTexture(GL_TEXTURE0+GLenum(unit));
    ++cache.stats.active_calls;
    cache.active_unit=(unit>=0 && unit<int(cached_submission_state::max_units))
        ?unit:-1;
}
void jkgm::gl::bind_texture_raw(enum_type target,uint_type texture) {
    auto &cache=submission_state;
    ++cache.stats.texture_calls;
    if(cache.active_unit<0) {
        GLint current=GL_TEXTURE0;
        glGetIntegerv(GL_ACTIVE_TEXTURE,&current);
        int const unit=current-GL_TEXTURE0;
        cache.active_unit=unit>=0 && unit<int(cached_submission_state::max_units)
            ?unit:-1;
    }
    if(cache.active_unit>=0) {
        auto &unit=cache.textures[size_t(cache.active_unit)];
        if(unit.generation!=cache.generation) {
            unit=cached_texture_unit{};
            unit.generation=cache.generation;
        }
        if(auto *slot=binding_slot(unit,GLenum(target))) {
            if(*slot==texture) {
                ++cache.stats.texture_skipped;
                return;
            }
            glBindTexture(GLenum(target),GLuint(texture));
            *slot=texture;
            return;
        }
    }
    glBindTexture(GLenum(target),GLuint(texture));
}
void jkgm::gl::bind_vertex_array_raw(uint_type vao) {
    auto &cache=submission_state;
    ++cache.stats.vao_calls;
    if(cache.vao==vao) {
        ++cache.stats.vao_skipped;
        return;
    }
    glBindVertexArray(GLuint(vao));
    cache.vao=vao;
}
void jkgm::gl::set_front_face_raw(enum_type winding) {
    auto &cache=submission_state;
    ++cache.stats.front_calls;
    if(cache.winding==winding) {
        ++cache.stats.front_skipped;
        return;
    }
    glFrontFace(GLenum(winding));
    cache.winding=GLenum(winding);
}
void jkgm::gl::set_cull_face_enabled(bool enabled) {
    auto &cache=submission_state;
    ++cache.stats.cull_calls;
    if(cache.cull_enabled==int(enabled)) {
        ++cache.stats.cull_skipped;
        return;
    }
    if(enabled)glEnable(GL_CULL_FACE);
    else glDisable(GL_CULL_FACE);
    cache.cull_enabled=int(enabled);
}

void jkgm::gl::enable(capability cap)
{
    if(cap==capability::cull_face) {
        set_cull_face_enabled(true);return;
    }
    glEnable(static_cast<GLenum>(cap));
}

void jkgm::gl::disable(capability cap)
{
    if(cap==capability::cull_face) {
        set_cull_face_enabled(false);return;
    }
    glDisable(static_cast<GLenum>(cap));
}

void jkgm::gl::clear(clear_flags cf)
{
    glClear(static_cast<GLbitfield>(cf));
}

void jkgm::gl::set_blend_function(blend_function sfactor, blend_function dfactor)
{
    glBlendFunc(static_cast<GLenum>(sfactor), static_cast<GLenum>(dfactor));
}

void jkgm::gl::set_blend_function_separate(blend_function src_rgb,
                                           blend_function dst_rgb,
                                           blend_function src_alpha,
                                           blend_function dst_alpha)
{
    glBlendFuncSeparate(static_cast<GLenum>(src_rgb),
                        static_cast<GLenum>(dst_rgb),
                        static_cast<GLenum>(src_alpha),
                        static_cast<GLenum>(dst_alpha));
}

void jkgm::gl::set_clear_color(color c)
{
    glClearColor(get<r>(c), get<g>(c), get<b>(c), get<a>(c));
}

void jkgm::gl::set_depth_function(comparison_function func)
{
    glDepthFunc(static_cast<GLenum>(func));
}

void jkgm::gl::set_depth_mask(bool enable)
{
    glDepthMask(enable ? GL_TRUE : GL_FALSE);
}

void jkgm::gl::set_face_cull_mode(face_mode mode)
{
    auto &cache=submission_state;
    ++cache.stats.cull_mode_calls;
    if(cache.cull_mode==static_cast<GLenum>(mode)) {
        ++cache.stats.cull_mode_skipped;return;
    }
    glCullFace(static_cast<GLenum>(mode));
    cache.cull_mode=static_cast<GLenum>(mode);
}

void jkgm::gl::set_min_sample_shading_factor(float f)
{
    glMinSampleShadingARB(f);
}

void jkgm::gl::set_polygon_mode(face_mode fm, polygon_mode pm)
{
    glPolygonMode(static_cast<GLenum>(fm), static_cast<GLenum>(pm));
}

void jkgm::gl::set_viewport(box<2, int> vp)
{
    auto dim = vp.stop - vp.start;
    glViewport(get<x>(vp.start), get<y>(vp.start), get<x>(dim), get<y>(dim));
}

void jkgm::gl::log_errors()
{
    for(;;) {
        auto e = glGetError();
        if(e == GL_NO_ERROR) {
            break;
        }

        LOG_ERROR("OpenGL error: ", hex(static_cast<size_t>(e)));
    }
}

namespace jkgm::gl {
    static_assert(blend_function::zero == blend_function(GL_ZERO));
    static_assert(blend_function::one == blend_function(GL_ONE));
    static_assert(blend_function::destination_color == blend_function(GL_DST_COLOR));
    static_assert(blend_function::one_minus_source_alpha == blend_function(GL_ONE_MINUS_SRC_ALPHA));

    static_assert(capability::blend == capability(GL_BLEND));
    static_assert(capability::cull_face == capability(GL_CULL_FACE));
    static_assert(capability::depth_test == capability(GL_DEPTH_TEST));
    static_assert(capability::framebuffer_srgb == capability(GL_FRAMEBUFFER_SRGB));
    static_assert(capability::multisample == capability(GL_MULTISAMPLE));
    static_assert(capability::sample_shading == capability(GL_SAMPLE_SHADING_ARB));

    static_assert(clear_flag::color == clear_flag(GL_COLOR_BUFFER_BIT));
    static_assert(clear_flag::depth == clear_flag(GL_DEPTH_BUFFER_BIT));

    static_assert(comparison_function::never == comparison_function(GL_NEVER));
    static_assert(comparison_function::less == comparison_function(GL_LESS));
    static_assert(comparison_function::equal == comparison_function(GL_EQUAL));
    static_assert(comparison_function::less_equal == comparison_function(GL_LEQUAL));
    static_assert(comparison_function::greater == comparison_function(GL_GREATER));
    static_assert(comparison_function::not_equal == comparison_function(GL_NOTEQUAL));
    static_assert(comparison_function::greater_equal == comparison_function(GL_GEQUAL));
    static_assert(comparison_function::always == comparison_function(GL_ALWAYS));

    static_assert(face_mode::front == face_mode(GL_FRONT));
    static_assert(face_mode::back == face_mode(GL_BACK));
    static_assert(face_mode::front_and_back == face_mode(GL_FRONT_AND_BACK));

    static_assert(polygon_mode::point == polygon_mode(GL_POINT));
    static_assert(polygon_mode::line == polygon_mode(GL_LINE));
    static_assert(polygon_mode::fill == polygon_mode(GL_FILL));
}
