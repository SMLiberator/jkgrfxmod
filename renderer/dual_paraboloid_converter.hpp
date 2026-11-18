#pragma once
// Temporary six-view cube -> two fixed +Z/-Z paraboloid texture layers.
// This conversion establishes a reference without nonlinear geometry errors.
// The eventual direct two-view capture can replace the conversion alone.
#include "offscreen_capture_target.hpp"
#include "dual_paraboloid_projection.hpp"
#include "base/log.hpp"
#include <algorithm>

namespace jkgm {
class dual_paraboloid_converter {
    GLuint program_=0U,vao_=0U;
    GLint faces_=-1,face_size_=-1,radius_=-1,hemisphere_=-1;
    bool failed_=false;
    static bool compile(GLuint shader,char const *source) {
        glShaderSource(shader,1,&source,nullptr);
        glCompileShader(shader);
        GLint ok=GL_FALSE;
        glGetShaderiv(shader,GL_COMPILE_STATUS,&ok);
        if(ok!=GL_TRUE) {
            GLchar log[2048]{};
            glGetShaderInfoLog(shader,2047,nullptr,log);
            LOG_WARNING("Dual paraboloid converter shader: ",log);
        }
        return ok==GL_TRUE;
    }
public:
    ~dual_paraboloid_converter(){release();}
    void release() {
        if(vao_)glDeleteVertexArrays(1,&vao_);
        if(program_)glDeleteProgram(program_);
        vao_=program_=0U;
    }
    bool ensure() {
        if(program_)return true;
        if(failed_)return false;
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
uniform sampler2DArray cube_faces;
uniform float face_resolution, disk_radius;
uniform int hemisphere;
vec4 sample_cube(vec3 d) {
    vec3 a=abs(d);
    float major=0.0,layer=0.0;
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
    vec2 uv=vec2(dot(d,right),dot(d,up))/max(major,1.e-6);
    vec2 inset=vec2(0.5/max(face_resolution,1.0));
    return textureLod(cube_faces,
        vec3(clamp(uv*0.5+0.5,inset,vec2(1.0)-inset),layer),0.0);
}
vec4 paraboloid_pixel(vec2 uv) {
    vec2 q=(uv*2.0-1.0)*disk_radius;
    float r2=dot(q,q);
    if(r2>disk_radius*disk_radius)return vec4(0.0);
    float sign_z=hemisphere==0?1.0:-1.0;
    // Inverse paraboloid projection. Local +Y reverses in the lower disk.
    vec3 direction=vec3(2.0*q.x,sign_z*2.0*q.y,
                        sign_z*(1.0-r2))/(1.0+r2);
    return sample_cube(direction);
}
void main() {
    // Supersample source cube edges. Preserve premultiplied RGB and coverage
    // alpha for roughness mips and the material EnvMap fallback.
    vec2 texel=vec2(1.0/float(textureSize(cube_faces,0).x*2));
    const vec2 offsets[4]=vec2[4](
        vec2(-0.25,-0.25),vec2(0.25,-0.25),
        vec2(-0.25,0.25),vec2(0.25,0.25));
    vec4 accum=vec4(0.0);
    for(int i=0;i<4;i++)
        accum+=paraboloid_pixel(image_uv+offsets[i]*texel);
    result_color=accum*0.25;
}
)GLSL";
        GLuint const vs=glCreateShader(GL_VERTEX_SHADER);
        GLuint const fs=glCreateShader(GL_FRAGMENT_SHADER);
        bool const compiled=compile(vs,vertex) && compile(fs,fragment);
        if(!compiled) {
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
            LOG_WARNING("Dual paraboloid converter program: ",log);
            release();failed_=true;return false;
        }
        faces_=glGetUniformLocation(program_,"cube_faces");
        face_size_=glGetUniformLocation(program_,"face_resolution");
        radius_=glGetUniformLocation(program_,"disk_radius");
        hemisphere_=glGetUniformLocation(program_,"hemisphere");
        glGenVertexArrays(1,&vao_);
        if(!vao_) {release();failed_=true;return false;}
        return true;
    }
    bool convert(reflection_probe_capture_target const &source,
                 reflection_probe_capture_target &destination,int side)
    {
        if(!source.ready() || source.layers()!=6 ||
           source.width()<1 || side<1 ||
           !destination.ensure(side,2) || !ensure())
            return false;
        offscreen_capture_gl_scope restore;
        GLint old_array=0;
        glActiveTexture(GL_TEXTURE0);
        glGetIntegerv(GL_TEXTURE_BINDING_2D_ARRAY,&old_array);
        glBindFramebuffer(GL_FRAMEBUFFER,destination.framebuffer());
        glViewport(0,0,side,side);
        glDisable(GL_DEPTH_TEST);
        glDisable(GL_BLEND);
        glDisable(GL_SCISSOR_TEST);
        glDisable(GL_STENCIL_TEST);
        glDisable(GL_CULL_FACE);
        glDisable(GL_FRAMEBUFFER_SRGB);
        glColorMask(GL_TRUE,GL_TRUE,GL_TRUE,GL_TRUE);
        glUseProgram(program_);
        glUniform1i(faces_,0);
        glUniform1f(face_size_,float(source.width()));
        glUniform1f(radius_,dual_paraboloid_disk_radius);
        glBindTexture(GL_TEXTURE_2D_ARRAY,source.color_texture());
        glBindVertexArray(vao_);
        bool succeeded=true;
        for(int layer=0;layer<2;++layer) {
            if(!destination.select_layer(layer)){succeeded=false;break;}
            glUniform1i(hemisphere_,layer);
            glDrawArrays(GL_TRIANGLES,0,3);
        }
        glBindTexture(GL_TEXTURE_2D_ARRAY,destination.color_texture());
        if(succeeded)glGenerateMipmap(GL_TEXTURE_2D_ARRAY);
        glBindTexture(GL_TEXTURE_2D_ARRAY,GLuint(old_array));
        return succeeded && glGetError()==GL_NO_ERROR;
    }
};
} // namespace jkgm
