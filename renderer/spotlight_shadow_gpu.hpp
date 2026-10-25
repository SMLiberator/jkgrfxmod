#pragma once
// Experimental static-world spotlight depth target. OpenGL objects are
// allocated only when the opt-in shadow setting is enabled. The level's
// polygons are uploaded once; subsequent moving-light updates draw only
// BVH-overlapping ranges, with no world-geometry CPU readback per frame.
#include "spotlight_shadow.hpp"
#include "base/log.hpp"
#include "glad/glad.h"
#include "glutil/buffer.hpp"
#include "glutil/framebuffer.hpp"
#include "glutil/texture.hpp"
#include "glutil/vertex_array.hpp"
#include <cstdint>
#include <limits>
#include <vector>

namespace jkgm {
struct spotlight_shadow_gpu {
    gl::framebuffer fbo;
    gl::texture depth;
    gl::vertex_array vao;
    gl::buffer vertices,indices;
    decal_world_geometry world{};
    spotlight_shadow_static_mesh mesh{};
    std::vector<spotlight_shadow_surface_range> selected;
    std::vector<GLsizei> counts;
    std::vector<void const *> offsets;
    unsigned resolution=spotlight_shadow_initial_resolution;
    bool complete=false;
    uintptr_t loaded_world=0;
    uint64_t revision=0;

    explicit spotlight_shadow_gpu(unsigned pixels=spotlight_shadow_initial_resolution)
    {
        resolution=std::max(64U,std::min(pixels,spotlight_shadow_max_resolution));
        gl::set_active_texture_unit(0);
        gl::bind_texture(gl::texture_bind_target::texture_2d,depth);
        gl::tex_image_2d(gl::texture_bind_target::texture_2d,0,
            gl::texture_internal_format::depth_component_16,
            make_size(int(resolution),int(resolution)),
            gl::texture_pixel_format::depth_component,
            gl::texture_pixel_type::float32,
            span<char const>(nullptr,0U));
        gl::set_texture_max_level(gl::texture_bind_target::texture_2d,0);
        gl::set_texture_min_filter(gl::texture_bind_target::texture_2d,
                                   gl::min_filter::nearest);
        gl::set_texture_mag_filter(gl::texture_bind_target::texture_2d,
                                   gl::mag_filter::nearest);
        gl::set_texture_wrap_mode(gl::texture_bind_target::texture_2d,
            gl::texture_direction::s,gl::texture_wrap_mode::clamp_to_border);
        gl::set_texture_wrap_mode(gl::texture_bind_target::texture_2d,
            gl::texture_direction::t,gl::texture_wrap_mode::clamp_to_border);
        gl::set_texture_border_color(gl::texture_bind_target::texture_2d,
                                     color(1.0f,1.0f,1.0f,1.0f));
        gl::set_texture_compare_mode(gl::texture_bind_target::texture_2d,
                                     gl::texture_compare_mode::none);
        gl::bind_framebuffer(gl::framebuffer_bind_target::any,fbo);
        gl::framebuffer_texture(gl::framebuffer_bind_target::any,
                                gl::framebuffer_attachment::depth,depth,0);
        gl::draw_buffers(gl::draw_buffer::none);
        glReadBuffer(GL_NONE);
        complete=gl::check_framebuffer_status(
                     gl::framebuffer_bind_target::any)==
                     gl::framebuffer_status::complete;
        if(!complete)LOG_WARNING("Spotlight shadow depth framebuffer incomplete");
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
        gl::bind_vertex_array(gl::vertex_array_view(0U));
    }

    // The callback must be a signature-guarded, bounded read of JK memory.
    bool update_world(decal_native_read const &read,uintptr_t address)
    {
        if(!complete || !address)return false;
        if(loaded_world==address && world.matches(read,address))return true;
        loaded_world=0;
        world.clear();
        mesh={};
        // Hidden-but-otherwise-valid surfaces must remain in this shadow
        // cache: SetSurfaceGeomode may switch them back on without changing
        // level topology. Visible decal receivers keep their usual filter.
        if(!world.load(read,address,250000U,true) ||
           !build_spotlight_shadow_static_mesh(world,mesh)) {
            world.clear();
            mesh={};
            return false;
        }
        gl::bind_vertex_array(vao);
        gl::bind_buffer(gl::buffer_bind_target::array,vertices);
        gl::buffer_data(gl::buffer_bind_target::array,
                        make_span(mesh.vertices).as_const_bytes(),
                        gl::buffer_usage::static_draw);
        gl::bind_buffer(gl::buffer_bind_target::element_array,indices);
        gl::buffer_data(gl::buffer_bind_target::element_array,
                        make_span(mesh.indices).as_const_bytes(),
                        gl::buffer_usage::static_draw);
        gl::bind_vertex_array(gl::vertex_array_view(0U));
        loaded_world=address;
        ++revision;
        LOG_INFO("Spotlight shadow static world ready surfaces=",mesh.surfaces.size(),
                 " indices=",mesh.indices.size());
        return true;
    }

    // The vertex shader/light uniforms and depth state must be bound by the
    // caller. This pass never needs material textures or a color attachment.
    unsigned draw_world(spotlight_shadow_projection const &projection)
    {
        if(!complete || !loaded_world)return 0;
        query_spotlight_shadow_static_mesh(world,mesh,projection,selected);
        counts.clear();offsets.clear();
        counts.reserve(selected.size());offsets.reserve(selected.size());
        for(auto const &range:selected) {
            if(range.count>uint32_t(std::numeric_limits<GLsizei>::max()))
                continue;
            counts.push_back(static_cast<GLsizei>(range.count));
            offsets.push_back(reinterpret_cast<void const *>(
                uintptr_t(range.first_index)*sizeof(uint32_t)));
        }
        if(counts.empty())return 0;
        gl::bind_vertex_array(vao);
        glMultiDrawElements(GL_TRIANGLES,counts.data(),GL_UNSIGNED_INT,
                            offsets.data(),static_cast<GLsizei>(counts.size()));
        return static_cast<unsigned>(counts.size());
    }
};
} // namespace jkgm
