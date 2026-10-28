// Regression tests for the redundant-uniform filter in glutil/program.cpp.
// Runs the REAL program.cpp against a mock GL: no OpenGL context is needed.
#include "glutil/program.hpp"
#include "glad/gl.h"
#include <cassert>
#include <cstdint>
#include <vector>

namespace {
    struct gl_call {
        char op; // 'i' = glUniformNi, 'f' = glUniformNf
        int location;
        int count;
        float v[4];
    };
    std::vector<gl_call> calls;
    GLuint next_program = 1U;
    GLuint forced_program = 0U; // when non-zero, glCreateProgram returns this

    GLuint APIENTRY mock_create_program() {
        if(forced_program != 0U) {
            GLuint const id = forced_program;
            forced_program = 0U;
            return id;
        }
        return next_program++;
    }
    void APIENTRY mock_delete_program(GLuint) {}
    void APIENTRY mock_link_program(GLuint) {}
    void APIENTRY mock_use_program(GLuint) {}
    void APIENTRY mock_u1i(GLint l, GLint a) { calls.push_back({'i', l, 1, {float(a), 0, 0, 0}}); }
    void APIENTRY mock_u2i(GLint l, GLint a, GLint b) { calls.push_back({'i', l, 2, {float(a), float(b), 0, 0}}); }
    void APIENTRY mock_u3i(GLint l, GLint a, GLint b, GLint c) { calls.push_back({'i', l, 3, {float(a), float(b), float(c), 0}}); }
    void APIENTRY mock_u4i(GLint l, GLint a, GLint b, GLint c, GLint d) { calls.push_back({'i', l, 4, {float(a), float(b), float(c), float(d)}}); }
    void APIENTRY mock_u1f(GLint l, GLfloat a) { calls.push_back({'f', l, 1, {a, 0, 0, 0}}); }
    void APIENTRY mock_u2f(GLint l, GLfloat a, GLfloat b) { calls.push_back({'f', l, 2, {a, b, 0, 0}}); }
    void APIENTRY mock_u3f(GLint l, GLfloat a, GLfloat b, GLfloat c) { calls.push_back({'f', l, 3, {a, b, c, 0}}); }
    void APIENTRY mock_u4f(GLint l, GLfloat a, GLfloat b, GLfloat c, GLfloat d) { calls.push_back({'f', l, 4, {a, b, c, d}}); }
}

// Entry points program.cpp uses. Everything else stays unset.
PFNGLCREATEPROGRAMPROC glad_glCreateProgram = mock_create_program;
PFNGLDELETEPROGRAMPROC glad_glDeleteProgram = mock_delete_program;
PFNGLLINKPROGRAMPROC glad_glLinkProgram = mock_link_program;
PFNGLUSEPROGRAMPROC glad_glUseProgram = mock_use_program;
PFNGLUNIFORM1IPROC glad_glUniform1i = mock_u1i;
PFNGLUNIFORM2IPROC glad_glUniform2i = mock_u2i;
PFNGLUNIFORM3IPROC glad_glUniform3i = mock_u3i;
PFNGLUNIFORM4IPROC glad_glUniform4i = mock_u4i;
PFNGLUNIFORM1FPROC glad_glUniform1f = mock_u1f;
PFNGLUNIFORM2FPROC glad_glUniform2f = mock_u2f;
PFNGLUNIFORM3FPROC glad_glUniform3f = mock_u3f;
PFNGLUNIFORM4FPROC glad_glUniform4f = mock_u4f;
// Referenced by program.cpp but never called by these tests.
PFNGLATTACHSHADERPROC glad_glAttachShader = nullptr;
PFNGLGETPROGRAMIVPROC glad_glGetProgramiv = nullptr;
PFNGLGETPROGRAMINFOLOGPROC glad_glGetProgramInfoLog = nullptr;
PFNGLGETUNIFORMLOCATIONPROC glad_glGetUniformLocation = nullptr;
PFNGLUNIFORMMATRIX4FVPROC glad_glUniformMatrix4fv = nullptr;

int main()
{
    using namespace jkgm;
    using gl::uniform_location_id;
    uniform_location_id const loc0(0), loc1(1), loc2(2), missing(-1);

    gl::program a;
    gl::program b;
    gl::use_program(a);

    // Same value twice: only the first reaches GL.
    gl::set_uniform_integer(loc0, 7);
    gl::set_uniform_integer(loc0, 7);
    assert(calls.size() == 1);
    // A different value goes through, and then is remembered.
    gl::set_uniform_integer(loc0, 8);
    gl::set_uniform_integer(loc0, 8);
    assert(calls.size() == 2 && calls.back().v[0] == 8.0f);

    // Locations are independent.
    gl::set_uniform_integer(loc1, 8);
    assert(calls.size() == 3);

    // The same bit pattern as an int must not suppress a float of the same
    // location (different uniform type), and floats compare exactly.
    gl::set_uniform_float(loc2, 0.5f);
    gl::set_uniform_float(loc2, 0.5f);
    assert(calls.size() == 4);
    gl::set_uniform_float(loc2, 0.5f + 1e-7f);
    assert(calls.size() == 5);
    gl::set_uniform_integer(loc2, 0);
    assert(calls.size() == 6);

    // Vectors: any changed component is sent, an identical vector is not.
    calls.clear();
    gl::set_uniform_vector(loc0, make_point(1.0f, 2.0f, 3.0f));
    gl::set_uniform_vector(loc0, make_point(1.0f, 2.0f, 3.0f));
    assert(calls.size() == 1 && calls[0].count == 3);
    gl::set_uniform_vector(loc0, make_point(1.0f, 2.0f, 4.0f));
    assert(calls.size() == 2);
    gl::set_uniform_vector(loc0, make_point(1.0f, 2.0f, 3.0f, 0.0f));
    assert(calls.size() == 3 && calls[2].count == 4); // different arity = different type
    gl::set_uniform_vector(loc0, make_point(1.0f, 2.0f, 3.0f, 0.0f));
    assert(calls.size() == 3);

    // Values are cached per PROGRAM: switching programs must not leak state.
    calls.clear();
    gl::set_uniform_integer(loc0, 5);
    assert(calls.size() == 1);
    gl::use_program(b);
    gl::set_uniform_integer(loc0, 5); // b has never been given this value
    assert(calls.size() == 2);
    gl::use_program(a);
    gl::set_uniform_integer(loc0, 5); // a still holds it
    assert(calls.size() == 2);

    // Relinking resets uniforms to their defaults, so the cache is dropped.
    gl::link_program(a);
    gl::set_uniform_integer(loc0, 5);
    assert(calls.size() == 3);
    gl::set_uniform_integer(loc0, 5);
    assert(calls.size() == 3);
    // ...but only for the relinked program.
    gl::use_program(b);
    gl::set_uniform_integer(loc0, 5);
    assert(calls.size() == 3);

    // A missing uniform (location -1) never produces a GL call at all.
    calls.clear();
    gl::set_uniform_integer(missing, 1);
    gl::set_uniform_float(missing, 1.0f);
    gl::set_uniform_vector(missing, make_point(1.0f, 2.0f));
    assert(calls.empty());

    // A recycled program id (delete + create) must start with a clean cache.
    {
        GLuint recycled = 0U;
        {
            gl::program temp;
            recycled = temp.value();
            gl::use_program(temp);
            gl::set_uniform_integer(loc0, 42);
            assert(calls.size() == 1);
        } // destroyed here
        forced_program = recycled;
        gl::program reused;
        assert(reused.value() == recycled);
        gl::use_program(reused);
        gl::set_uniform_integer(loc0, 42);
        assert(calls.size() == 2); // not suppressed by the dead program's value
    }

    // With no program bound nothing is cached, so nothing is wrongly skipped.
    calls.clear();
    gl::use_program(gl::program_view(0U));
    gl::set_uniform_integer(loc0, 3);
    gl::set_uniform_integer(loc0, 3);
    assert(calls.size() == 2);

    return 0;
}
