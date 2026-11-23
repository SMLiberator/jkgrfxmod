#pragma once
// One-shot GL 3.3 cubemap-array -> 2:1 equirectangular texture conversion.
// No CPU readbacks. Samples in scene-linear space, with seam-aware angular
// supersampling across the six independently mipmapped 90-degree faces.
#include "offscreen_capture_target.hpp"
#include "glad/glad.h"
#include "base/log.hpp"
#include <algorithm>
#include <array>
#include <cmath>

namespace jkgm {
class global_env_equirect_converter {
    GLuint program_=0U,vao_=0U,rebase_program_=0U;
    GLint cube_slot_=-1,face_resolution_=-1,fog_color_=-1;
    GLint rebase_from_=-1,rebase_to_=-1,rebase_mix_=-1;
    bool failed_=false,rebase_failed_=false;
    static bool compile(GLuint shader,char const *source) {
        glShaderSource(shader,1,&source,nullptr);
        glCompileShader(shader);
        GLint ok=GL_FALSE;
        glGetShaderiv(shader,GL_COMPILE_STATUS,&ok);
        if(ok!=GL_TRUE) {
            GLchar log[2048]{};
            glGetShaderInfoLog(shader,2047,nullptr,log);
            LOG_WARNING("CaptureEnvMap converter shader: ",log);
        }
        return ok==GL_TRUE;
    }
public:
    ~global_env_equirect_converter(){release();}
    void release() {
        if(vao_)glDeleteVertexArrays(1,&vao_);
        if(program_)glDeleteProgram(program_);
        if(rebase_program_)glDeleteProgram(rebase_program_);
        vao_=program_=rebase_program_=0U;
    }
    bool ensure() {
        if(program_)return true;
        if(failed_)return false;
        static char const *vertex=R"GLSL(#version 330 core
out vec2 image_uv;
void main() {
    // Fullscreen triangle, without vertex buffers.
    vec2 p=vec2((gl_VertexID<<1)&2,gl_VertexID&2);
    image_uv=p;
    gl_Position=vec4(p*2.0-1.0,0.0,1.0);
}
)GLSL";
        static char const *fragment=R"GLSL(#version 330 core
in vec2 image_uv;
out vec4 result_color;
uniform sampler2DArray cube_faces;
uniform float face_resolution;
uniform vec3 empty_color;
const float PI=3.14159265358979323846;
vec3 direction_for(vec2 uv) {
    float lon=(uv.x-0.5)*2.0*PI;
    float lat=(0.5-uv.y)*PI;
    float c=cos(lat);
    return vec3(sin(lon)*c,cos(lon)*c,sin(lat));
}
vec4 sample_face(vec3 d) {
    vec3 a=abs(d);
    float major=0.0;
    float layer=0.0;
    vec3 right=vec3(0.0),up=vec3(0.0);
    if(a.x>=a.y && a.x>=a.z) {
        major=a.x;
        if(d.x>=0.0) {layer=0.0;right=vec3(0.0,-1.0,0.0);}
        else {layer=1.0;right=vec3(0.0,1.0,0.0);}
        up=vec3(0.0,0.0,1.0);
    } else if(a.y>=a.z) {
        major=a.y;
        if(d.y>=0.0) {layer=2.0;right=vec3(1.0,0.0,0.0);}
        else {layer=3.0;right=vec3(-1.0,0.0,0.0);}
        up=vec3(0.0,0.0,1.0);
    } else {
        major=a.z;
        if(d.z>=0.0) {layer=4.0;right=vec3(0.0,1.0,0.0);}
        else {layer=5.0;right=vec3(0.0,-1.0,0.0);}
        up=vec3(1.0,0.0,0.0);
    }
    vec2 uv=vec2(dot(d,right),dot(d,up))/max(major,1e-6);
    vec2 inset=vec2(0.5/max(face_resolution,1.0));
    return textureLod(cube_faces,
        vec3(clamp(uv*0.5+0.5,inset,vec2(1.0)-inset),layer),0.0);
}
void main() {
    // Supersampling across the output texel footprint avoids hard cube face
    // seams even though the source is six separate perspective images.
    vec2 texel=vec2(1.0/(face_resolution*4.0),
                    1.0/(face_resolution*2.0));
    vec3 sum=vec3(0.0);
    const vec2 offsets[4]=vec2[4](
        vec2(-0.25,-0.25),vec2(0.25,-0.25),
        vec2(-0.25,0.25),vec2(0.25,0.25));
    for(int i=0;i<4;i++) {
        vec4 c=sample_face(direction_for(image_uv+offsets[i]*texel));
        // In case capture geometry leaves holes, fill with world fog color.
        // Do it BEFORE mip generation so uncovered texels cannot bleed black.
        sum+=mix(empty_color,c.rgb,c.a);
    }
    result_color=vec4(sum*0.25,1.0);
}
)GLSL";
        GLuint const vs=glCreateShader(GL_VERTEX_SHADER);
        GLuint const fs=glCreateShader(GL_FRAGMENT_SHADER);
        if(!compile(vs,vertex) || !compile(fs,fragment)) {
            glDeleteShader(vs);glDeleteShader(fs);
            failed_=true;return false;
        }
        program_=glCreateProgram();
        glAttachShader(program_,vs);glAttachShader(program_,fs);
        glLinkProgram(program_);
        glDeleteShader(vs);glDeleteShader(fs);
        GLint ok=GL_FALSE;
        glGetProgramiv(program_,GL_LINK_STATUS,&ok);
        if(ok!=GL_TRUE) {
            GLchar log[2048]{};
            glGetProgramInfoLog(program_,2047,nullptr,log);
            LOG_WARNING("CaptureEnvMap converter program: ",log);
            release();failed_=true;return false;
        }
        cube_slot_=glGetUniformLocation(program_,"cube_faces");
        face_resolution_=glGetUniformLocation(program_,"face_resolution");
        fog_color_=glGetUniformLocation(program_,"empty_color");
        glGenVertexArrays(1,&vao_);
        return vao_!=0U;
    }
    // The ordinary dual-EnvMap shader cannot express an overlapping fade
    // (old A->B, followed by new C) with only two samplers. Resolve the
    // currently displayed A/B mixture ONCE to HDR, then fade that to C.
    // This is strictly on-demand, not a per-frame reflection render pass.
    bool ensure_rebase() {
        if(rebase_program_)return true;
        if(rebase_failed_ || !ensure())return false;
        static char const *vertex=R"GLSL(#version 330 core
out vec2 image_uv;
void main() {
    vec2 p=vec2((gl_VertexID<<1)&2,gl_VertexID&2);
    image_uv=p;
    gl_Position=vec4(p*2.0-1.0,0.0,1.0);
}
)GLSL";
        static char const *fragment=R"GLSL(#version 330 core
in vec2 image_uv;
out vec4 result_color;
uniform sampler2D old_from;
uniform sampler2D old_to;
uniform float old_mix;
void main() {
    vec3 a=textureLod(old_from,image_uv,0.0).rgb;
    vec3 b=textureLod(old_to,image_uv,0.0).rgb;
    result_color=vec4(mix(a,b,old_mix),1.0);
}
)GLSL";
        GLuint const vs=glCreateShader(GL_VERTEX_SHADER);
        GLuint const fs=glCreateShader(GL_FRAGMENT_SHADER);
        if(!compile(vs,vertex) || !compile(fs,fragment)) {
            glDeleteShader(vs);glDeleteShader(fs);
            rebase_failed_=true;return false;
        }
        GLuint const prog=glCreateProgram();
        glAttachShader(prog,vs);
        glAttachShader(prog,fs);
        glLinkProgram(prog);
        glDeleteShader(vs);
        glDeleteShader(fs);
        GLint ok=GL_FALSE;
        glGetProgramiv(prog,GL_LINK_STATUS,&ok);
        if(ok!=GL_TRUE) {
            GLchar log[2048]{};
            glGetProgramInfoLog(prog,2047,nullptr,log);
            LOG_WARNING("CaptureEnvMap fade rebase shader: ",log);
            glDeleteProgram(prog);
            rebase_failed_=true;return false;
        }
        rebase_from_=glGetUniformLocation(prog,"old_from");
        rebase_to_=glGetUniformLocation(prog,"old_to");
        rebase_mix_=glGetUniformLocation(prog,"old_mix");
        rebase_program_=prog;
        return true;
    }

    // Preserve the displayed HDR envmap exactly when a new capture
    // interrupts an unfinished fade. Supports changed capture resolutions.
    bool rebase(offscreen_capture_target const &from,
                offscreen_capture_target const &to,
                offscreen_capture_target &destination,float blend) {
        if(!from.ready() || !to.ready() ||
           !std::isfinite(blend) || blend<0.0f || blend>1.0f ||
           !destination.ensure(to.width(),to.height()) ||
           !ensure_rebase())return false;
        offscreen_capture_gl_scope restore;
        glBindFramebuffer(GL_FRAMEBUFFER,destination.framebuffer());
        glViewport(0,0,destination.width(),destination.height());
        glDisable(GL_DEPTH_TEST);
        glDisable(GL_BLEND);
        glDisable(GL_SCISSOR_TEST);
        glDisable(GL_CULL_FACE);
        glDisable(GL_STENCIL_TEST);
        glDisable(GL_FRAMEBUFFER_SRGB);
        glColorMask(GL_TRUE,GL_TRUE,GL_TRUE,GL_TRUE);
        glUseProgram(rebase_program_);
        glUniform1i(rebase_from_,0);
        glUniform1i(rebase_to_,1);
        glUniform1f(rebase_mix_,blend);
        glActiveTexture(GL_TEXTURE0);
        glBindTexture(GL_TEXTURE_2D,from.color_texture());
        glActiveTexture(GL_TEXTURE1);
        glBindTexture(GL_TEXTURE_2D,to.color_texture());
        glBindVertexArray(vao_);
        glDrawArrays(GL_TRIANGLES,0,3);
        glActiveTexture(GL_TEXTURE0);
        GLint prior_2d=0;
        glGetIntegerv(GL_TEXTURE_BINDING_2D,&prior_2d);
        glBindTexture(GL_TEXTURE_2D,destination.color_texture());
        glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_WRAP_S,GL_REPEAT);
        glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_WRAP_T,GL_CLAMP_TO_EDGE);
        glGenerateMipmap(GL_TEXTURE_2D);
        glBindTexture(GL_TEXTURE_2D,GLuint(prior_2d));
        return glGetError()==GL_NO_ERROR;
    }

    bool convert(reflection_probe_capture_target const &source,
                 offscreen_capture_target &destination,int side,
                 std::array<float,3> const &empty_color) {
        if(!source.ready() || source.layers()!=6 || side<1 ||
           !destination.ensure(4*side,2*side) || !ensure())
            return false;
        offscreen_capture_gl_scope restore;
        GLint prior_array=0;
        glActiveTexture(GL_TEXTURE0);
        glGetIntegerv(GL_TEXTURE_BINDING_2D_ARRAY,&prior_array);
        glBindFramebuffer(GL_FRAMEBUFFER,destination.framebuffer());
        glViewport(0,0,4*side,2*side);
        glDisable(GL_DEPTH_TEST);
        glDisable(GL_BLEND);
        glDisable(GL_SCISSOR_TEST);
        glDisable(GL_CULL_FACE);
        glDisable(GL_STENCIL_TEST);
        glDisable(GL_FRAMEBUFFER_SRGB);
        glColorMask(GL_TRUE,GL_TRUE,GL_TRUE,GL_TRUE);
        glUseProgram(program_);
        glUniform1i(cube_slot_,0);
        glUniform1f(face_resolution_,float(side));
        glUniform3f(fog_color_,empty_color[0],empty_color[1],empty_color[2]);
        glBindTexture(GL_TEXTURE_2D_ARRAY,source.color_texture());
        glBindVertexArray(vao_);
        glDrawArrays(GL_TRIANGLES,0,3);
        glBindTexture(GL_TEXTURE_2D_ARRAY,GLuint(prior_array));
        // Equirectangular longitude wraps across the left/right seam.
        GLint prior_2d=0;
        glGetIntegerv(GL_TEXTURE_BINDING_2D,&prior_2d);
        glBindTexture(GL_TEXTURE_2D,destination.color_texture());
        glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_WRAP_S,GL_REPEAT);
        glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_WRAP_T,GL_CLAMP_TO_EDGE);
        glGenerateMipmap(GL_TEXTURE_2D);
        glBindTexture(GL_TEXTURE_2D,GLuint(prior_2d));
        return glGetError()==GL_NO_ERROR;
    }
};
} // namespace jkgm
