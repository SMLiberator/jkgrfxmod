#pragma once
// Experimental sky-only SUNLIGHT APERTURE depth map. Separately rasterize
// the authored sky polygons (SithSurface flags 0x200/0x400); NEVER
// reclassify hidden Things as casters or draw geometry behind the player.
// One GL_DEPTH_COMPONENT24 2-layer texture stores the closest sky face on
// each sunlight ray. A receiver must be farther from the sun than the
// aperture to receive added sunlight. Normal caster shadows remain separate.
#include "sun_shadow.hpp"
#include "spotlight_shadow.hpp"
#include "glutil/buffer.hpp"
#include "glutil/framebuffer.hpp"
#include "glutil/texture.hpp"
#include "glutil/vertex_array.hpp"
#include "glad/gl.h"
#include <algorithm>
#include <array>
#include <cstdint>
#include <cstddef>
#include <utility>
#include <vector>

namespace jkgm {
struct sun_sky_aperture_gpu {
    gl::texture depth; // GL_TEXTURE_2D_ARRAY, near/far layers 0/1
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
    unsigned resolution=0;
    bool complete=false;

    explicit sun_sky_aperture_gpu(unsigned pixels)
    {
        resolution=std::max(64U,std::min(pixels,4096U));
        GLint old_texture=0,old_active_texture=0;
        GLint old_draw_fbo=0,old_read_fbo=0,old_vao=0,old_array_buffer=0;
        glGetIntegerv(GL_ACTIVE_TEXTURE,&old_active_texture);
        glGetIntegerv(GL_TEXTURE_BINDING_2D_ARRAY,&old_texture);
        glGetIntegerv(GL_DRAW_FRAMEBUFFER_BINDING,&old_draw_fbo);
        glGetIntegerv(GL_READ_FRAMEBUFFER_BINDING,&old_read_fbo);
        glGetIntegerv(GL_VERTEX_ARRAY_BINDING,&old_vao);
        glGetIntegerv(GL_ARRAY_BUFFER_BINDING,&old_array_buffer);
        glBindTexture(GL_TEXTURE_2D_ARRAY,depth.value());
        glTexImage3D(GL_TEXTURE_2D_ARRAY,0,GL_DEPTH_COMPONENT24,
            GLsizei(resolution),GLsizei(resolution),2,0,
            GL_DEPTH_COMPONENT,GL_FLOAT,nullptr);
        glTexParameteri(GL_TEXTURE_2D_ARRAY,GL_TEXTURE_MIN_FILTER,GL_NEAREST);
        glTexParameteri(GL_TEXTURE_2D_ARRAY,GL_TEXTURE_MAG_FILTER,GL_NEAREST);
        glTexParameteri(GL_TEXTURE_2D_ARRAY,GL_TEXTURE_WRAP_S,GL_CLAMP_TO_BORDER);
        glTexParameteri(GL_TEXTURE_2D_ARRAY,GL_TEXTURE_WRAP_T,GL_CLAMP_TO_BORDER);
        glTexParameteri(GL_TEXTURE_2D_ARRAY,GL_TEXTURE_COMPARE_MODE,GL_NONE);
        glTexParameteri(GL_TEXTURE_2D_ARRAY,GL_TEXTURE_BASE_LEVEL,0);
        glTexParameteri(GL_TEXTURE_2D_ARRAY,GL_TEXTURE_MAX_LEVEL,0);
        GLfloat const white[4]{1.0f,1.0f,1.0f,1.0f};
        glTexParameterfv(GL_TEXTURE_2D_ARRAY,GL_TEXTURE_BORDER_COLOR,white);
        complete=true;
        for(int c=0;c<2;++c) {
            gl::bind_framebuffer(gl::framebuffer_bind_target::any,fbo[c]);
            glFramebufferTextureLayer(GL_FRAMEBUFFER,GL_DEPTH_ATTACHMENT,
                                      depth.value(),0,c);
            glDrawBuffer(GL_NONE);
            glReadBuffer(GL_NONE);
            complete=complete &&
                glCheckFramebufferStatus(GL_FRAMEBUFFER)==GL_FRAMEBUFFER_COMPLETE;
        }
        gl::bind_framebuffer(gl::framebuffer_bind_target::any,
                             gl::default_framebuffer);
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
        gl::bind_vertex_array(gl::vertex_array_view(GLuint(old_vao)));
        glBindBuffer(GL_ARRAY_BUFFER,GLuint(old_array_buffer));
        glBindFramebuffer(GL_DRAW_FRAMEBUFFER,GLuint(old_draw_fbo));
        glBindFramebuffer(GL_READ_FRAMEBUFFER,GLuint(old_read_fbo));
        glBindTexture(GL_TEXTURE_2D_ARRAY,GLuint(old_texture));
        glActiveTexture(GLenum(old_active_texture));
    }

    bool update_world(decal_native_read const &read,uintptr_t native_world)
    {
        if(!complete || !native_world)return false;
        if(loaded_world==native_world && world.matches(read,native_world))
            return true;
        loaded_world=0;
        valid={{false,false}};
        // Use the *existing* validated native SithWorld snapshot format.
        // Sky polygons are real world-coordinate apertures, not projected
        // skybox texture UVs; include passable adjoins for those openings.
        if(!world.load(read,native_world,250000U,true,true,true))
            return false;
        std::vector<decal_world_surface_geometry> skies;
        for(auto &surface:world.receivers)
            if((surface.surface_flags&0x600U)!=0U)
                skies.push_back(std::move(surface));
        world.receivers=std::move(skies);
        // The original BVH is not needed: the GPU clips sky triangles to
        // the current orthographic viewport at raster time.
        world.acceleration={};
        if(!build_spotlight_shadow_static_mesh(world,mesh))return false;
        GLint old_vao=0,old_array_buffer=0;
        glGetIntegerv(GL_VERTEX_ARRAY_BINDING,&old_vao);
        glGetIntegerv(GL_ARRAY_BUFFER_BINDING,&old_array_buffer);
        gl::bind_vertex_array(vao);
        gl::bind_buffer(gl::buffer_bind_target::array,vertices);
        gl::buffer_data(gl::buffer_bind_target::array,
            make_span(mesh.vertices).as_const_bytes(),
            gl::buffer_usage::static_draw);
        gl::bind_buffer(gl::buffer_bind_target::element_array,indices);
        gl::buffer_data(gl::buffer_bind_target::element_array,
            make_span(mesh.indices).as_const_bytes(),
            gl::buffer_usage::static_draw);
        gl::bind_vertex_array(gl::vertex_array_view(GLuint(old_vao)));
        glBindBuffer(GL_ARRAY_BUFFER,GLuint(old_array_buffer));
        loaded_world=native_world;
        ++revision;
        return true;
    }

    // Hash live sky flags and GeoModes, since COG can change those even
    // though the cached vertex/index arrays are immutable.
    sun_shadow_projection expanded_sky_view(
        sun_shadow_projection const &ordinary) const
    {
        // Do not increase the ordinary shadow depth interval (which hurts
        // VSM precision). Enlarge only the dedicated sky aperture range.
        auto adjusted=ordinary;
        float min_z=decal_dot(ordinary.origin,ordinary.forward);
        float max_z=min_z+ordinary.range;
        for(auto const &face:world.receivers)
            for(auto const &v:face.polygon) {
                float z=decal_dot(v,ordinary.forward);
                min_z=std::min(min_z,z);
                max_z=std::max(max_z,z);
            }
        // Preserve a visible clear-depth border even in very large
        // levels. A fixed 0.01-JKU pad can round to 1.0 in a 24-bit
        // normalized map when the world spans many kilometers.
        float const margin=std::max(0.01f,(max_z-min_z)*0.001f);
        min_z-=margin;max_z+=margin;
        for(int i=0;i<3;++i)
            adjusted.origin[i]+=ordinary.forward[i]*(
                min_z-decal_dot(ordinary.origin,ordinary.forward));
        adjusted.range=max_z-min_z;
        return adjusted;
    }

    // A scale and bias to convert the receiver's *ordinary* normalized
    // sunlight depth into the aperture map's normalized depth.
    std::array<float,2> depth_transform(
        int cascade,sun_shadow_projection const &ordinary) const
    {
        auto const &sky=views[cascade];
        float const old_z=decal_dot(ordinary.origin,ordinary.forward);
        float const sky_z=decal_dot(sky.origin,sky.forward);
        return {{ordinary.range/sky.range,(old_z-sky_z)/sky.range}};
    }

    uint64_t live_signature(decal_native_read const &read) const
    {
        uint64_t h=14695981039346656037ULL;
        for(auto const &surface:world.receivers) {
            uintptr_t const base=world.surfaces_addr+
                uintptr_t(surface.surface_index)*0x5cU;
            uint32_t flags=0,geo=0;
            if(!read(base+0x10U,&flags,4U) ||
               !read(base+0x1cU,&geo,4U)) {
                flags=0;geo=0;
            }
            for(uint32_t v:{surface.surface_index,
                            flags&0x600U,geo}) {
                h^=uint64_t(v);
                h*=1099511628211ULL;
            }
        }
        return h;
    }
};
} // namespace jkgm
