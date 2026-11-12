#pragma once
// A dedicated non-MSAA color/depth target for one planar capture.
// Uses no native rdCamera, no G-buffer and no main framebuffer attachments.
#include "glad/glad.h"
#include <algorithm>

namespace jkgm {
class planar_reflection_capture_target {
    GLuint fbo_=0,color_=0,depth_=0;
    int width_=0,height_=0;
    bool failed_=false;
public:
    planar_reflection_capture_target()=default;
    planar_reflection_capture_target(planar_reflection_capture_target const &)=delete;
    planar_reflection_capture_target &
        operator=(planar_reflection_capture_target const &)=delete;
    ~planar_reflection_capture_target(){release();}
    void release() {
        if(depth_)glDeleteRenderbuffers(1,&depth_);
        if(color_)glDeleteTextures(1,&color_);
        if(fbo_)glDeleteFramebuffers(1,&fbo_);
        depth_=color_=fbo_=0;
        width_=height_=0;
    }
    bool ensure(int width,int height) {
        if(width<=0 || height<=0 || width>16384 || height>16384)
            return false;
        if(failed_ && width==width_ && height==height_)return false;
        if(fbo_ && width_==width && height_==height)return true;
        GLint max_size=0;
        glGetIntegerv(GL_MAX_TEXTURE_SIZE,&max_size);
        if(width>max_size || height>max_size)return false;
        GLint old_draw=0,old_read=0,old_active=0,old_tex=0,old_rb=0;
        glGetIntegerv(GL_DRAW_FRAMEBUFFER_BINDING,&old_draw);
        glGetIntegerv(GL_READ_FRAMEBUFFER_BINDING,&old_read);
        glGetIntegerv(GL_ACTIVE_TEXTURE,&old_active);
        glActiveTexture(GL_TEXTURE0);
        glGetIntegerv(GL_TEXTURE_BINDING_2D,&old_tex);
        glGetIntegerv(GL_RENDERBUFFER_BINDING,&old_rb);
        release();
        width_=width;height_=height;failed_=false;
        glGenFramebuffers(1,&fbo_);
        glGenTextures(1,&color_);
        glGenRenderbuffers(1,&depth_);
        glBindFramebuffer(GL_FRAMEBUFFER,fbo_);
        glBindTexture(GL_TEXTURE_2D,color_);
        // Linear color, with depth for proper reflected world/3DO occlusion.
        glTexImage2D(GL_TEXTURE_2D,0,GL_RGBA8,width,height,0,
                     GL_RGBA,GL_UNSIGNED_BYTE,nullptr);
        glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_MIN_FILTER,GL_LINEAR);
        glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_MAG_FILTER,GL_LINEAR);
        glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_WRAP_S,GL_CLAMP_TO_EDGE);
        glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_WRAP_T,GL_CLAMP_TO_EDGE);
        glFramebufferTexture2D(GL_FRAMEBUFFER,GL_COLOR_ATTACHMENT0,
                               GL_TEXTURE_2D,color_,0);
        glBindRenderbuffer(GL_RENDERBUFFER,depth_);
        glRenderbufferStorage(GL_RENDERBUFFER,GL_DEPTH_COMPONENT24,width,height);
        glFramebufferRenderbuffer(GL_FRAMEBUFFER,GL_DEPTH_ATTACHMENT,
                                  GL_RENDERBUFFER,depth_);
        GLenum const draw=GL_COLOR_ATTACHMENT0;
        glDrawBuffers(1,&draw);
        glReadBuffer(GL_COLOR_ATTACHMENT0);
        GLenum const status=glCheckFramebufferStatus(GL_FRAMEBUFFER);
        glBindFramebuffer(GL_DRAW_FRAMEBUFFER,GLuint(old_draw));
        glBindFramebuffer(GL_READ_FRAMEBUFFER,GLuint(old_read));
        glBindRenderbuffer(GL_RENDERBUFFER,GLuint(old_rb));
        glBindTexture(GL_TEXTURE_2D,GLuint(old_tex));
        glActiveTexture(GLenum(old_active));
        if(status!=GL_FRAMEBUFFER_COMPLETE) {
            release();width_=width;height_=height;failed_=true;
            return false;
        }
        return true;
    }
    GLuint framebuffer()const{return fbo_;}
    GLuint color_texture()const{return color_;}
    int width()const{return width_;}
    int height()const{return height_;}
    bool ready()const{return fbo_!=0 && color_!=0 && depth_!=0;}
};

// Scoped GL state for the experimental pass. The legacy renderer expects
// very particular program/texture/VAO/blend/depth state after its draws.
class planar_reflection_gl_scope {
    GLint draw_=0,read_=0,viewport_[4]{},program_=0,vao_=0;
    GLint active_=0,texture0_=0,front_=GL_CCW,cull_=GL_BACK;
    GLint depth_func_=GL_LESS,blend_src_rgb_=GL_ONE,blend_dst_rgb_=GL_ZERO;
    GLint blend_src_alpha_=GL_ONE,blend_dst_alpha_=GL_ZERO;
    GLboolean depth_mask_=GL_TRUE,color_mask_[4]{};
    GLfloat clear_color_[4]{};
    GLboolean blend_=GL_FALSE,depth_test_=GL_FALSE,cull_test_=GL_FALSE;
    GLboolean scissor_=GL_FALSE,framebuffer_srgb_=GL_FALSE;
    GLboolean polygon_offset_=GL_FALSE;
public:
    planar_reflection_gl_scope() {
        glGetIntegerv(GL_DRAW_FRAMEBUFFER_BINDING,&draw_);
        glGetIntegerv(GL_READ_FRAMEBUFFER_BINDING,&read_);
        glGetIntegerv(GL_VIEWPORT,viewport_);
        glGetIntegerv(GL_CURRENT_PROGRAM,&program_);
        glGetIntegerv(GL_VERTEX_ARRAY_BINDING,&vao_);
        glGetIntegerv(GL_ACTIVE_TEXTURE,&active_);
        glActiveTexture(GL_TEXTURE0);
        glGetIntegerv(GL_TEXTURE_BINDING_2D,&texture0_);
        glActiveTexture(GLenum(active_));
        glGetIntegerv(GL_FRONT_FACE,&front_);
        glGetIntegerv(GL_CULL_FACE_MODE,&cull_);
        glGetIntegerv(GL_DEPTH_FUNC,&depth_func_);
        glGetIntegerv(GL_BLEND_SRC_RGB,&blend_src_rgb_);
        glGetIntegerv(GL_BLEND_DST_RGB,&blend_dst_rgb_);
        glGetIntegerv(GL_BLEND_SRC_ALPHA,&blend_src_alpha_);
        glGetIntegerv(GL_BLEND_DST_ALPHA,&blend_dst_alpha_);
        glGetBooleanv(GL_DEPTH_WRITEMASK,&depth_mask_);
        glGetBooleanv(GL_COLOR_WRITEMASK,color_mask_);
        glGetFloatv(GL_COLOR_CLEAR_VALUE,clear_color_);
        blend_=glIsEnabled(GL_BLEND);
        depth_test_=glIsEnabled(GL_DEPTH_TEST);
        cull_test_=glIsEnabled(GL_CULL_FACE);
        scissor_=glIsEnabled(GL_SCISSOR_TEST);
        framebuffer_srgb_=glIsEnabled(GL_FRAMEBUFFER_SRGB);
        polygon_offset_=glIsEnabled(GL_POLYGON_OFFSET_FILL);
    }
    planar_reflection_gl_scope(planar_reflection_gl_scope const &)=delete;
    planar_reflection_gl_scope &
        operator=(planar_reflection_gl_scope const &)=delete;
    ~planar_reflection_gl_scope(){
        glBindFramebuffer(GL_DRAW_FRAMEBUFFER,GLuint(draw_));
        glBindFramebuffer(GL_READ_FRAMEBUFFER,GLuint(read_));
        glViewport(viewport_[0],viewport_[1],viewport_[2],viewport_[3]);
        glUseProgram(GLuint(program_));
        glBindVertexArray(GLuint(vao_));
        glActiveTexture(GL_TEXTURE0);
        glBindTexture(GL_TEXTURE_2D,GLuint(texture0_));
        glActiveTexture(GLenum(active_));
        glFrontFace(GLenum(front_));
        glCullFace(GLenum(cull_));
        glDepthFunc(GLenum(depth_func_));
        glBlendFuncSeparate(GLenum(blend_src_rgb_),GLenum(blend_dst_rgb_),
                            GLenum(blend_src_alpha_),GLenum(blend_dst_alpha_));
        glDepthMask(depth_mask_);
        glClearColor(clear_color_[0],clear_color_[1],
                     clear_color_[2],clear_color_[3]);
        glColorMask(color_mask_[0],color_mask_[1],
                    color_mask_[2],color_mask_[3]);
        auto restore=[](GLenum capability,GLboolean enabled) {
            if(enabled)glEnable(capability);else glDisable(capability);
        };
        restore(GL_BLEND,blend_);
        restore(GL_DEPTH_TEST,depth_test_);
        restore(GL_CULL_FACE,cull_test_);
        restore(GL_SCISSOR_TEST,scissor_);
        restore(GL_FRAMEBUFFER_SRGB,framebuffer_srgb_);
        restore(GL_POLYGON_OFFSET_FILL,polygon_offset_);
    }
};
} // namespace jkgm
