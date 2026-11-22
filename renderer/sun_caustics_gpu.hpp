#pragma once
// Separate nearest-fluid SUN depth+coefficient layers, projected with the
// SAME camera-local near/far views as ordinary sunlight shadow maps.
// Fluid world passable-adjoin polygons are retained; sprites NEVER enter.
#include "sun_caustics.hpp"
#include "sun_shadow.hpp"
#include "spotlight_shadow.hpp"
#include "base/log.hpp"
#include "glad/glad.h"
#include "glutil/buffer.hpp"
#include "glutil/framebuffer.hpp"
#include "glutil/texture.hpp"
#include "glutil/vertex_array.hpp"
#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
namespace jkgm {
struct sun_caustics_gpu {
    gl::texture depth,density,pattern;
    std::array<gl::framebuffer,2> fbo;
    gl::vertex_array vao;
    gl::buffer vertices,indices;
    decal_world_geometry world{};
    spotlight_shadow_static_mesh mesh{};
    uintptr_t loaded_world=0;
    uint64_t revision=0;
    std::array<bool,2> valid{{false,false}};
    std::array<uint64_t,2> signatures{};
    std::array<sun_shadow_projection,2> views{};
    unsigned resolution=512U;
    bool complete=false;

    explicit sun_caustics_gpu(unsigned pixels=512U)
    {
        resolution=std::max(64U,std::min(pixels,1024U));
        GLint active=0,old_array=0,old_2d=0,draw=0,read=0,vao_prev=0,vbo=0;
        glGetIntegerv(GL_ACTIVE_TEXTURE,&active);
        gl::set_active_texture_unit_raw(0);
        glGetIntegerv(GL_TEXTURE_BINDING_2D_ARRAY,&old_array);
        glGetIntegerv(GL_TEXTURE_BINDING_2D,&old_2d);
        glGetIntegerv(GL_DRAW_FRAMEBUFFER_BINDING,&draw);
        glGetIntegerv(GL_READ_FRAMEBUFFER_BINDING,&read);
        glGetIntegerv(GL_VERTEX_ARRAY_BINDING,&vao_prev);
        glGetIntegerv(GL_ARRAY_BUFFER_BINDING,&vbo);
        auto allocate=[&](gl::texture &tex,GLenum format,GLenum external) {
            gl::bind_texture_raw(GL_TEXTURE_2D_ARRAY,tex.value());
            glTexImage3D(GL_TEXTURE_2D_ARRAY,0,format,GLsizei(resolution),
                         GLsizei(resolution),2,0,external,GL_FLOAT,nullptr);
            glTexParameteri(GL_TEXTURE_2D_ARRAY,GL_TEXTURE_MIN_FILTER,GL_NEAREST);
            glTexParameteri(GL_TEXTURE_2D_ARRAY,GL_TEXTURE_MAG_FILTER,GL_NEAREST);
            glTexParameteri(GL_TEXTURE_2D_ARRAY,GL_TEXTURE_WRAP_S,GL_CLAMP_TO_BORDER);
            glTexParameteri(GL_TEXTURE_2D_ARRAY,GL_TEXTURE_WRAP_T,GL_CLAMP_TO_BORDER);
            glTexParameteri(GL_TEXTURE_2D_ARRAY,GL_TEXTURE_BASE_LEVEL,0);
            glTexParameteri(GL_TEXTURE_2D_ARRAY,GL_TEXTURE_MAX_LEVEL,0);
        };
        allocate(depth,GL_DEPTH_COMPONENT24,GL_DEPTH_COMPONENT);
        glTexParameteri(GL_TEXTURE_2D_ARRAY,GL_TEXTURE_COMPARE_MODE,GL_NONE);
        // R=Fluid thickness; GBA=water MAT RGB at first sun intersection.
        allocate(density,GL_RGBA16F,GL_RGBA);
        complete=true;
        for(int layer=0;layer<2;++layer) {
            gl::bind_framebuffer(gl::framebuffer_bind_target::any,fbo[layer]);
            glFramebufferTextureLayer(GL_FRAMEBUFFER,GL_DEPTH_ATTACHMENT,
                                      depth.value(),0,layer);
            glFramebufferTextureLayer(GL_FRAMEBUFFER,GL_COLOR_ATTACHMENT0,
                                      density.value(),0,layer);
            glDrawBuffer(GL_COLOR_ATTACHMENT0);
            glReadBuffer(GL_NONE);
            complete=complete &&
                glCheckFramebufferStatus(GL_FRAMEBUFFER)==GL_FRAMEBUFFER_COMPLETE;
        }
        // Deterministic procedural texture; no uploaded or runtime image file.
        auto const noise=generate_sun_caustic_pattern();
        gl::bind_texture_raw(GL_TEXTURE_2D,pattern.value());
        glTexImage2D(GL_TEXTURE_2D,0,GL_R8,
                     sun_caustic_pattern_size,sun_caustic_pattern_size,
                     0,GL_RED,GL_UNSIGNED_BYTE,noise.data());
        glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_WRAP_S,GL_REPEAT);
        glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_WRAP_T,GL_REPEAT);
        glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_MIN_FILTER,GL_LINEAR_MIPMAP_LINEAR);
        glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_MAG_FILTER,GL_LINEAR);
        glGenerateMipmap(GL_TEXTURE_2D);
        gl::bind_vertex_array(vao);
        gl::bind_buffer(gl::buffer_bind_target::array,vertices);
        gl::enable_vertex_attrib_array(0);
        gl::vertex_attrib_pointer(0,3,gl::vertex_element_type::float32,
            false,sizeof(spotlight_shadow_vertex),
            offsetof(spotlight_shadow_vertex,position));
        gl::enable_vertex_attrib_array(1);
        gl::vertex_attrib_pointer(1,2,gl::vertex_element_type::float32,
            false,sizeof(spotlight_shadow_vertex),
            offsetof(spotlight_shadow_vertex,uv));
        gl::bind_buffer(gl::buffer_bind_target::element_array,indices);
        gl::bind_vertex_array(gl::vertex_array_view(GLuint(vao_prev)));
        glBindBuffer(GL_ARRAY_BUFFER,GLuint(vbo));
        glBindFramebuffer(GL_DRAW_FRAMEBUFFER,GLuint(draw));
        glBindFramebuffer(GL_READ_FRAMEBUFFER,GLuint(read));
        gl::bind_texture_raw(GL_TEXTURE_2D_ARRAY,GLuint(old_array));
        gl::bind_texture_raw(GL_TEXTURE_2D,GLuint(old_2d));
        gl::set_active_texture_unit_raw(active-GL_TEXTURE0);
        if(!complete)LOG_WARNING("Sun Fluid caustics framebuffer incomplete");
    }

    bool update_world(decal_native_read const &read,uintptr_t current)
    {
        if(!complete || !current)return false;
        if(loaded_world==current && world.matches(read,current))return true;
        loaded_world=0;
        world.clear();
        mesh={};
        valid={{false,false}};
        // Include hidden in SNAPSHOT only. GeoMode is tested live on each
        // caustic capture. Water often occupies passable adjoins.
        if(!world.load(read,current,250000U,true,false,true) ||
           !build_spotlight_shadow_static_mesh(world,mesh)) {
            world.clear();mesh={};return false;
        }
        gl::bind_vertex_array(vao);
        gl::bind_buffer(gl::buffer_bind_target::array,vertices);
        gl::buffer_data(gl::buffer_bind_target::array,
            make_span(mesh.vertices).as_const_bytes(),gl::buffer_usage::static_draw);
        gl::bind_buffer(gl::buffer_bind_target::element_array,indices);
        gl::buffer_data(gl::buffer_bind_target::element_array,
            make_span(mesh.indices).as_const_bytes(),gl::buffer_usage::static_draw);
        gl::bind_vertex_array(gl::vertex_array_view(0U));
        loaded_world=current;
        ++revision;
        return true;
    }
};
} // namespace jkgm
