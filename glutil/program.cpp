#include "program.hpp"
#include "glad/gl.h"
#include <cstdint>
#include <cstring>
#include <vector>

namespace {
    // Redundant-uniform filter.
    //
    // A uniform's value belongs to its program object and persists until the
    // program is relinked or deleted, so setting the same value again is a
    // pure waste of a GL call. That matters: the 3DO/triangle draw loops
    // re-send the same sampler bindings, blend modes and material constants
    // for every draw range, and each GL call is considerably more expensive
    // under Wine (it crosses the wine opengl32 thunk layer) than natively.
    //
    // Every non-array uniform in the renderer goes through the setters in
    // this file, and programs are only ever bound through use_program() and
    // only ever linked through link_program(), so tracking the current
    // program here is exact. The cache is dropped for a program when it is
    // linked (which resets its uniforms) and when it is destroyed (its id may
    // be reused by glCreateProgram).
    struct uniform_slot {
        uint32_t bits[4] = {0U, 0U, 0U, 0U};
        uint8_t type = 0; // 0 = never set; 1-4 = ivecN; 5-8 = vecN
    };

    constexpr int max_cached_location = 2048;
    constexpr size_t max_cached_program = 4096;

    std::vector<std::vector<uniform_slot>> uniform_slots; // by program id
    GLuint current_program = 0U;

    void forget_program_uniforms(GLuint id)
    {
        if(id < uniform_slots.size()) {
            uniform_slots[id].clear();
        }
    }

    // True when the GL call can be skipped. Otherwise remembers the new value
    // (the caller issues the GL call immediately afterwards).
    bool uniform_unchanged(int location, uint8_t type, uint32_t const (&bits)[4])
    {
        // GL silently ignores location -1 (uniform optimized out or absent).
        if(location < 0) {
            return true;
        }
        if(current_program == 0U || location >= max_cached_location ||
           current_program >= max_cached_program) {
            return false;
        }
        if(current_program >= uniform_slots.size()) {
            uniform_slots.resize(size_t(current_program) + 1U);
        }
        auto &slots = uniform_slots[current_program];
        if(slots.size() <= size_t(location)) {
            slots.resize(size_t(location) + 1U);
        }
        auto &slot = slots[size_t(location)];
        if(slot.type == type && std::memcmp(slot.bits, bits, sizeof(bits)) == 0) {
            return true;
        }
        slot.type = type;
        std::memcpy(slot.bits, bits, sizeof(bits));
        return false;
    }

    uint32_t float_bits(float v)
    {
        uint32_t b = 0U;
        std::memcpy(&b, &v, sizeof(b));
        return b;
    }
}

GLuint jkgm::gl::program_traits::create()
{
    return glCreateProgram();
}

void jkgm::gl::program_traits::destroy(GLuint id)
{
    forget_program_uniforms(id);
    glDeleteProgram(id);
}

void jkgm::gl::attach_shader(program_view id, shader_view s)
{
    glAttachShader(*id, *s);
}

void jkgm::gl::link_program(program_view id)
{
    glLinkProgram(*id);
    // Linking resets every uniform to its default value.
    forget_program_uniforms(*id);
}

bool jkgm::gl::get_program_link_status(program_view id)
{
    GLint result = GL_FALSE;
    glGetProgramiv(*id, GL_LINK_STATUS, &result);

    return result != GL_FALSE;
}

std::string jkgm::gl::get_program_info_log(program_view id)
{
    std::string rv;

    int log_length = 0;
    glGetProgramiv(*id, GL_INFO_LOG_LENGTH, &log_length);
    if(log_length > 0) {
        rv.resize(log_length, 'X');
        glGetProgramInfoLog(*id, log_length, nullptr, rv.data());
    }

    return rv;
}

void jkgm::gl::use_program(program_view prog)
{
    current_program = *prog;
    glUseProgram(*prog);
}

jkgm::gl::uniform_location_id jkgm::gl::get_uniform_location(program_view prog, cstring_view name)
{
    return uniform_location_id(glGetUniformLocation(*prog, name.c_str()));
}

void jkgm::gl::detail::inner_set_uniform_matrix_4fv(uniform_location_id uniform,
                                                   bool transpose,
                                                   std::array<float, 16> const &value)
{
    glUniformMatrix4fv(uniform.get(), /*count*/ 1U, to_gl_bool(transpose), value.data());
}

void jkgm::gl::set_uniform_integer(uniform_location_id uniform, int value)
{
    detail::inner_set_uniform_vec(uniform, value);
}

void jkgm::gl::set_uniform_float(uniform_location_id uniform, float value)
{
    detail::inner_set_uniform_vec(uniform, value);
}

void jkgm::gl::detail::inner_set_uniform_vec(uniform_location_id uniform, int v0)
{
    uint32_t const bits[4] = {uint32_t(v0), 0U, 0U, 0U};
    if(uniform_unchanged(uniform.get(), 1U, bits)) return;
    glUniform1i(uniform.get(), v0);
}

void jkgm::gl::detail::inner_set_uniform_vec(uniform_location_id uniform, int v0, int v1)
{
    uint32_t const bits[4] = {uint32_t(v0), uint32_t(v1), 0U, 0U};
    if(uniform_unchanged(uniform.get(), 2U, bits)) return;
    glUniform2i(uniform.get(), v0, v1);
}

void jkgm::gl::detail::inner_set_uniform_vec(uniform_location_id uniform, int v0, int v1, int v2)
{
    uint32_t const bits[4] = {uint32_t(v0), uint32_t(v1), uint32_t(v2), 0U};
    if(uniform_unchanged(uniform.get(), 3U, bits)) return;
    glUniform3i(uniform.get(), v0, v1, v2);
}

void jkgm::gl::detail::inner_set_uniform_vec(uniform_location_id uniform,
                                            int v0,
                                            int v1,
                                            int v2,
                                            int v3)
{
    uint32_t const bits[4] = {uint32_t(v0), uint32_t(v1), uint32_t(v2), uint32_t(v3)};
    if(uniform_unchanged(uniform.get(), 4U, bits)) return;
    glUniform4i(uniform.get(), v0, v1, v2, v3);
}

void jkgm::gl::detail::inner_set_uniform_vec(uniform_location_id uniform, float v0)
{
    uint32_t const bits[4] = {float_bits(v0), 0U, 0U, 0U};
    if(uniform_unchanged(uniform.get(), 5U, bits)) return;
    glUniform1f(uniform.get(), v0);
}

void jkgm::gl::detail::inner_set_uniform_vec(uniform_location_id uniform, float v0, float v1)
{
    uint32_t const bits[4] = {float_bits(v0), float_bits(v1), 0U, 0U};
    if(uniform_unchanged(uniform.get(), 6U, bits)) return;
    glUniform2f(uniform.get(), v0, v1);
}

void jkgm::gl::detail::inner_set_uniform_vec(uniform_location_id uniform,
                                            float v0,
                                            float v1,
                                            float v2)
{
    uint32_t const bits[4] = {float_bits(v0), float_bits(v1), float_bits(v2), 0U};
    if(uniform_unchanged(uniform.get(), 7U, bits)) return;
    glUniform3f(uniform.get(), v0, v1, v2);
}

void jkgm::gl::detail::inner_set_uniform_vec(uniform_location_id uniform,
                                            float v0,
                                            float v1,
                                            float v2,
                                            float v3)
{
    uint32_t const bits[4] = {float_bits(v0), float_bits(v1), float_bits(v2), float_bits(v3)};
    if(uniform_unchanged(uniform.get(), 8U, bits)) return;
    glUniform4f(uniform.get(), v0, v1, v2, v3);
}
