#pragma once
// Opt-in persistent SithWorld geometry cache and sector-first staging.
// Opt-in GPU-first opaque pass uses native depth-tested fallback.
// Native JK portal visibility and HOM remain authoritative.
#include "world_cache.hpp"
#include "world_cache_descriptors.hpp"
#include "texture_units.hpp"
#include "world_cache_visibility.hpp"
#include "world_cache_portals.hpp"
#include "world_cache_lighting.hpp"
#include "world_cache_uv_animation.hpp"
#include "planar_reflection_portals.hpp"
#include "base/log.hpp"
#include "glutil/buffer.hpp"
#include "glutil/vertex_array.hpp"
#include "glutil/texture.hpp"
#include "glad/gl.h"
#include <cstddef>
#include <cstdint>
#include <chrono>
#include <optional>

namespace jkgm {
struct world_cache_gpu {
    int descriptors_requested=0,descriptor_mode=0;
    bool descriptors_enabled=false; // descriptor_mode>0
    std::optional<gl::buffer> surface_descriptor_buffer;
    std::optional<gl::texture> surface_descriptor_texture;
    std::optional<gl::buffer> sector_descriptor_buffer;
    std::optional<gl::texture> sector_descriptor_texture;
    uintptr_t sector_table=0;
    uint32_t sector_count=0;
    std::vector<world_cache_surface_sector_descriptor> frame_surface_sector_descriptors;
    std::vector<world_cache_surface_sector_descriptor> uploaded_surface_sector_descriptors;
    std::vector<world_cache_sector_descriptor> frame_sector_descriptors;
    std::vector<world_cache_sector_descriptor> uploaded_sector_descriptors;
    std::vector<uint32_t> staged_sector_indices;
    std::vector<world_cache_surface_descriptor> frame_surface_descriptors;
    std::vector<world_cache_surface_descriptor> uploaded_surface_descriptors;
    void bind_surface_descriptors() const {
        if(!descriptors_enabled || !surface_descriptor_texture)return;
        gl::set_active_texture_unit_raw(texture_units::forward_world_surface_descriptors);
        gl::bind_texture_raw(GL_TEXTURE_BUFFER,surface_descriptor_texture->value());
        if(descriptor_mode==2 && sector_descriptor_texture) {
            gl::set_active_texture_unit_raw(texture_units::forward_world_sector_descriptors);
            gl::bind_texture_raw(GL_TEXTURE_BUFFER,sector_descriptor_texture->value());
        }
        gl::set_active_texture_unit_raw(0);
    }
    gl::vertex_array vao;
    gl::buffer vertices,indices,lighting,light_state,sector_tint,sector_cmp_tint,uv_animation;
    std::vector<std::array<float,3>> frame_light_rgb,frame_light_state,frame_sector_tint,frame_sector_cmp_tint;
    std::vector<std::array<float,2>> frame_uv_offset;
    // Exact last-uploaded attribute mirrors. Updated only after GL writes,
    // so a face hidden for several frames cannot reuse stale GPU contents.
    std::vector<std::array<float,3>> uploaded_light_rgb,uploaded_light_state;
    std::vector<std::array<float,3>> uploaded_sector_tint,uploaded_sector_cmp_tint;
    std::vector<std::array<float,2>> uploaded_uv_offset;
    std::vector<float> native_dynamic_light;
    decal_world_geometry world{};
    world_cache_mesh mesh{};
    std::vector<world_cache_draw_candidate> frame_candidates;
    std::vector<world_cache_draw_candidate> sector_candidates;
    std::vector<world_cache_draw_candidate> sector_special_candidates;
    std::vector<world_cache_draw_candidate> sector_sky_candidates;
    // Successful live lighting/UV-staged faces reused in forward alpha.
    // No native reads or vertex uploads are repeated in that pass.
    std::vector<world_cache_draw_candidate> frame_render_candidates;
    std::vector<uint8_t> portal_surface_membership;
    std::vector<world_cache_sector_span> visible_sector_spans;
    std::vector<uint32_t> cached_surface_lookup,sector_seen_epochs;
    std::vector<uint32_t> native_visible_sector_scratch;
    planar_reflection_portal_graph reflected_portal_graph;
    uint32_t sector_seen_epoch=0;
    uintptr_t loaded_world=0;
    uint64_t revision=0;
    bool ready=false;

    explicit world_cache_gpu(int request_descriptors=0)
        :descriptors_requested(request_descriptors)
    {
        gl::bind_vertex_array(vao);
        gl::bind_buffer(gl::buffer_bind_target::array,vertices);
        gl::enable_vertex_attrib_array(0);
        gl::vertex_attrib_pointer(0,3,gl::vertex_element_type::float32,
            false,sizeof(world_cache_vertex),
            offsetof(world_cache_vertex,position));
        gl::enable_vertex_attrib_array(1);
        gl::vertex_attrib_pointer(1,2,gl::vertex_element_type::float32,
            false,sizeof(world_cache_vertex),
            offsetof(world_cache_vertex,uv));
        gl::enable_vertex_attrib_array(2);
        gl::vertex_attrib_pointer(2,3,gl::vertex_element_type::float32,
            false,sizeof(world_cache_vertex),
            offsetof(world_cache_vertex,normal));
        gl::bind_buffer(gl::buffer_bind_target::array,lighting);
        gl::enable_vertex_attrib_array(3);
        gl::vertex_attrib_pointer(3,3,gl::vertex_element_type::float32,
            false,sizeof(std::array<float,3>),0);
        gl::bind_buffer(gl::buffer_bind_target::array,light_state);
        gl::enable_vertex_attrib_array(4);
        gl::vertex_attrib_pointer(4,3,gl::vertex_element_type::float32,
            false,sizeof(std::array<float,3>),0);
        gl::bind_buffer(gl::buffer_bind_target::array,sector_tint);
        gl::enable_vertex_attrib_array(5);
        gl::vertex_attrib_pointer(5,3,gl::vertex_element_type::float32,
            false,sizeof(std::array<float,3>),0);
        gl::bind_buffer(gl::buffer_bind_target::array,sector_cmp_tint);
        gl::enable_vertex_attrib_array(7);
        gl::vertex_attrib_pointer(7,3,gl::vertex_element_type::float32,
            false,sizeof(std::array<float,3>),0);
        gl::bind_buffer(gl::buffer_bind_target::array,uv_animation);
        gl::enable_vertex_attrib_array(6);
        gl::vertex_attrib_pointer(6,2,gl::vertex_element_type::float32,
            false,sizeof(std::array<float,2>),0);
        gl::bind_buffer(gl::buffer_bind_target::element_array,indices);
        gl::bind_vertex_array(gl::vertex_array_view(0U));
    }

    bool update_world(decal_native_read const &read,uintptr_t address)
    {
        if(!address)return false;
        if(ready && loaded_world==address &&
           world.matches(read,address))return true;
        auto const start=std::chrono::steady_clock::now();
        ready=false;
        loaded_world=0;
        world.clear();
        mesh={};
        frame_light_rgb.clear();
        frame_light_state.clear();
        frame_sector_tint.clear();
        frame_sector_cmp_tint.clear();
        frame_uv_offset.clear();
        descriptors_enabled=false;
        descriptor_mode=0;
        sector_table=0;
        sector_count=0;
        frame_surface_sector_descriptors.clear();
        uploaded_surface_sector_descriptors.clear();
        frame_sector_descriptors.clear();
        uploaded_sector_descriptors.clear();
        staged_sector_indices.clear();
        sector_descriptor_texture.reset();
        sector_descriptor_buffer.reset();
        frame_surface_descriptors.clear();
        uploaded_surface_descriptors.clear();
        surface_descriptor_texture.reset();
        surface_descriptor_buffer.reset();
        uploaded_light_rgb.clear();
        uploaded_light_state.clear();
        uploaded_sector_tint.clear();
        uploaded_sector_cmp_tint.clear();
        uploaded_uv_offset.clear();
        native_dynamic_light.clear();
        frame_candidates.clear();
        sector_candidates.clear();
        sector_special_candidates.clear();
        sector_sky_candidates.clear();
        frame_render_candidates.clear();
        portal_surface_membership.clear();
        visible_sector_spans.clear();
        native_visible_sector_scratch.clear();
        reflected_portal_graph.clear();
        cached_surface_lookup.clear();
        sector_seen_epochs.clear();
        sector_seen_epoch=0;
        // The render-only snapshot retains sky and passable-adjoin faces.
        // Selection still excludes sky until dedicated rendering matches JK.
        // Shadow/decal reader defaults still exclude pass-through portals.
        if(!world.load(read,address,250000U,true,true,true) ||
           !build_world_cache_mesh(world,mesh) ||
           !build_world_cache_surface_lookup(
               mesh,world.surface_count,cached_surface_lookup)) {
            world.clear();mesh={};
            return false;
        }
        sector_seen_epochs.assign(world.surface_count,0U);
        std::vector<world_cache_descriptor_vertex> compact_vertices;
        std::vector<world_cache_surface_descriptor> compact_descriptors;
        if(descriptors_requested>0) {
            GLint combined_units=0,vertex_units=0,max_texels=0;
            glGetIntegerv(GL_MAX_COMBINED_TEXTURE_IMAGE_UNITS,&combined_units);
            glGetIntegerv(GL_MAX_VERTEX_TEXTURE_IMAGE_UNITS,&vertex_units);
            glGetIntegerv(GL_MAX_TEXTURE_BUFFER_SIZE,&max_texels);
            bool const compact_ready=vertex_units>0 &&
                combined_units>texture_units::forward_world_surface_descriptors &&
                build_world_cache_descriptors(mesh,compact_vertices,
                                              compact_descriptors);
            if(compact_ready && descriptors_requested>=2 &&
               combined_units>texture_units::forward_world_sector_descriptors) {
                uint32_t count=0,table=0;
                if(read_decal_u32(read,address+0x4cU,count) &&
                   read_decal_u32(read,address+0x50U,table) &&
                   count>0 && count<=65536U &&
                   decal_native_address_ok(table) &&
                   uint64_t(table)+uint64_t(count)*0x98U<0x7ff00000U &&
                   compact_descriptors.size()*2U<=
                       size_t(std::max(max_texels,0)) &&
                   uint64_t(count)*2U<=uint64_t(std::max(max_texels,0))) {
                    descriptor_mode=2;
                    sector_table=table;
                    sector_count=count;
                    build_world_cache_sector_surface_descriptors(
                        compact_descriptors,frame_surface_sector_descriptors);
                    uploaded_surface_sector_descriptors=frame_surface_sector_descriptors;
                    frame_sector_descriptors.resize(count);
                    uploaded_sector_descriptors=frame_sector_descriptors;
                } else LOG_WARNING(
                    "WORLD CACHE sector descriptors unsupported; falling back to per-surface mode");
            }
            if(compact_ready && descriptor_mode==0 &&
               compact_descriptors.size()*3U<=size_t(std::max(max_texels,0))) {
                descriptor_mode=1;
                frame_surface_descriptors=std::move(compact_descriptors);
                uploaded_surface_descriptors=frame_surface_descriptors;
            }
            descriptors_enabled=descriptor_mode!=0;
            if(!descriptors_enabled)
                LOG_WARNING("WORLD CACHE surface descriptors unavailable; using legacy vertex layout");
        }
        gl::bind_vertex_array(vao);
        gl::bind_buffer(gl::buffer_bind_target::array,vertices);
        if(descriptors_enabled) {
            gl::buffer_data(gl::buffer_bind_target::array,
                make_span(compact_vertices).as_const_bytes(),
                gl::buffer_usage::static_draw);
            // Full FP32 XYZ and UVs stay at locations 0/1, which keeps
            // capture, sun-shadow and occlusion depth shaders unchanged.
            gl::vertex_attrib_pointer(0,3,gl::vertex_element_type::float32,
                false,sizeof(world_cache_descriptor_vertex),
                offsetof(world_cache_descriptor_vertex,position));
            gl::vertex_attrib_pointer(1,2,gl::vertex_element_type::float32,
                false,sizeof(world_cache_descriptor_vertex),
                offsetof(world_cache_descriptor_vertex,uv));
            glDisableVertexAttribArray(2);
            gl::enable_vertex_attrib_array(8);
            // Up to 250,000 surfaces: losslessly represented as float32 ID.
            gl::vertex_attrib_pointer(8,1,gl::vertex_element_type::uint32,
                false,sizeof(world_cache_descriptor_vertex),
                offsetof(world_cache_descriptor_vertex,surface));
            glDisableVertexAttribArray(5);
            glDisableVertexAttribArray(6);
            glDisableVertexAttribArray(7);
        } else {
            gl::buffer_data(gl::buffer_bind_target::array,
                make_span(mesh.vertices).as_const_bytes(),
                gl::buffer_usage::static_draw);
            gl::vertex_attrib_pointer(0,3,gl::vertex_element_type::float32,
                false,sizeof(world_cache_vertex),offsetof(world_cache_vertex,position));
            gl::vertex_attrib_pointer(1,2,gl::vertex_element_type::float32,
                false,sizeof(world_cache_vertex),offsetof(world_cache_vertex,uv));
            gl::enable_vertex_attrib_array(2);
            gl::vertex_attrib_pointer(2,3,gl::vertex_element_type::float32,
                false,sizeof(world_cache_vertex),offsetof(world_cache_vertex,normal));
            glDisableVertexAttribArray(8);
            gl::enable_vertex_attrib_array(5);
            gl::enable_vertex_attrib_array(6);
            gl::enable_vertex_attrib_array(7);
        }
        frame_light_rgb.assign(mesh.vertices.size(),{{1.0f,1.0f,1.0f}});
        uploaded_light_rgb=frame_light_rgb;
        gl::bind_buffer(gl::buffer_bind_target::array,lighting);
        gl::buffer_data(gl::buffer_bind_target::array,
            make_span(frame_light_rgb).as_const_bytes(),
            gl::buffer_usage::stream_draw);
        frame_light_state.assign(mesh.vertices.size(),{{1.0f,1.0f,0.0f}});
        uploaded_light_state=frame_light_state;
        gl::bind_buffer(gl::buffer_bind_target::array,light_state);
        gl::buffer_data(gl::buffer_bind_target::array,
            make_span(frame_light_state).as_const_bytes(),
            gl::buffer_usage::stream_draw);
        frame_sector_tint.assign(mesh.vertices.size(),{{0.0f,0.0f,0.0f}});
        uploaded_sector_tint=frame_sector_tint;
        if(!descriptors_enabled) {
            gl::bind_buffer(gl::buffer_bind_target::array,sector_tint);
            gl::buffer_data(gl::buffer_bind_target::array,
                make_span(frame_sector_tint).as_const_bytes(),
                gl::buffer_usage::stream_draw);
        }
        frame_sector_cmp_tint.assign(mesh.vertices.size(),{{1.0f,1.0f,1.0f}});
        uploaded_sector_cmp_tint=frame_sector_cmp_tint;
        if(!descriptors_enabled) {
            gl::bind_buffer(gl::buffer_bind_target::array,sector_cmp_tint);
            gl::buffer_data(gl::buffer_bind_target::array,
                make_span(frame_sector_cmp_tint).as_const_bytes(),
                gl::buffer_usage::stream_draw);
        }
        frame_uv_offset.assign(mesh.vertices.size(),{{0.0f,0.0f}});
        uploaded_uv_offset=frame_uv_offset;
        if(!descriptors_enabled) {
            gl::bind_buffer(gl::buffer_bind_target::array,uv_animation);
            gl::buffer_data(gl::buffer_bind_target::array,
                make_span(frame_uv_offset).as_const_bytes(),
                gl::buffer_usage::stream_draw);
        }
        if(descriptors_enabled) {
            surface_descriptor_buffer.emplace();
            surface_descriptor_texture.emplace();
            glBindBuffer(GL_TEXTURE_BUFFER,surface_descriptor_buffer->value());
            if(descriptor_mode==2)
                glBufferData(GL_TEXTURE_BUFFER,
                    GLsizeiptr(frame_surface_sector_descriptors.size()*
                              sizeof(frame_surface_sector_descriptors[0])),
                    frame_surface_sector_descriptors.data(),GL_STREAM_DRAW);
            else
                glBufferData(GL_TEXTURE_BUFFER,
                    GLsizeiptr(frame_surface_descriptors.size()*
                              sizeof(frame_surface_descriptors[0])),
                    frame_surface_descriptors.data(),GL_STREAM_DRAW);
            gl::set_active_texture_unit_raw(texture_units::forward_world_surface_descriptors);
            gl::bind_texture_raw(GL_TEXTURE_BUFFER,surface_descriptor_texture->value());
            glTexBuffer(GL_TEXTURE_BUFFER,GL_RGBA32F,
                        surface_descriptor_buffer->value());
            gl::bind_texture_raw(GL_TEXTURE_BUFFER,0U);
            if(descriptor_mode==2) {
                sector_descriptor_buffer.emplace();
                sector_descriptor_texture.emplace();
                glBindBuffer(GL_TEXTURE_BUFFER,sector_descriptor_buffer->value());
                glBufferData(GL_TEXTURE_BUFFER,
                    GLsizeiptr(frame_sector_descriptors.size()*
                              sizeof(frame_sector_descriptors[0])),
                    frame_sector_descriptors.data(),GL_STREAM_DRAW);
                gl::set_active_texture_unit_raw(
                    texture_units::forward_world_sector_descriptors);
                gl::bind_texture_raw(GL_TEXTURE_BUFFER,
                                      sector_descriptor_texture->value());
                glTexBuffer(GL_TEXTURE_BUFFER,GL_RGBA32F,
                            sector_descriptor_buffer->value());
                gl::bind_texture_raw(GL_TEXTURE_BUFFER,0U);
            }
            gl::set_active_texture_unit_raw(0);
            glBindBuffer(GL_TEXTURE_BUFFER,0U);
        }
        gl::bind_buffer(gl::buffer_bind_target::element_array,indices);
        gl::buffer_data(gl::buffer_bind_target::element_array,
            make_span(mesh.indices).as_const_bytes(),
            gl::buffer_usage::static_draw);
        gl::bind_vertex_array(gl::vertex_array_view(0U));
        loaded_world=address;
        ready=true;
        ++revision;
        LOG_INFO("WORLD CACHE phase0 built world=",address,
                 " surfaces=",mesh.surfaces.size(),
                 " uv_ready=",mesh.uv_ready_surfaces,
                 " missing_uv=",mesh.missing_uv_surfaces,
                 " cached_sky=",mesh.sky_surfaces,
                 " cached_passable_adjoins=",mesh.passable_adjoin_surfaces,
                 " skipped_portals=",world.skipped_portals,
                 " skipped_sky=",world.skipped_sky,
                 " skipped_material=",world.skipped_material,
                 " invalid_surfaces=",world.invalid_surfaces,
                 " native_surface_count=",world.surface_count,
                 " lookup_entries=",cached_surface_lookup.size(),
                 " vertices=",mesh.vertices.size(),
                 " triangles=",mesh.indices.size()/3U,
                 " bytes=",(descriptors_enabled
                     ?mesh.vertices.size()*sizeof(world_cache_descriptor_vertex)+
                      (descriptor_mode==2
                       ?frame_surface_sector_descriptors.size()*
                           sizeof(world_cache_surface_sector_descriptor)+
                        frame_sector_descriptors.size()*
                           sizeof(world_cache_sector_descriptor)
                       :frame_surface_descriptors.size()*
                           sizeof(world_cache_surface_descriptor))
                     :mesh.vertices.size()*sizeof(world_cache_vertex))+
                           mesh.indices.size()*sizeof(uint32_t)+
                           (frame_light_rgb.size()+frame_light_state.size()+
                            (descriptors_enabled?0U:frame_sector_tint.size()+
                                frame_sector_cmp_tint.size()))*
                               sizeof(std::array<float,3>)+
                           (descriptors_enabled?0U:frame_uv_offset.size()*
                               sizeof(std::array<float,2>)),
                 " descriptor_mode=",descriptor_mode,
                 " surface_descriptor_count=",descriptor_mode==2
                     ?frame_surface_sector_descriptors.size()
                     :frame_surface_descriptors.size(),
                 " sector_descriptor_count=",frame_sector_descriptors.size(),
                 " uv_animation_bytes=",descriptors_enabled?0U:
                     frame_uv_offset.size()*sizeof(std::array<float,2>),
                 " revision=",revision,
                 " build_us=",std::chrono::duration_cast<
                     std::chrono::microseconds>(
                         std::chrono::steady_clock::now()-start).count(),
                 " mode=world_cache_gpu_first_optional");
        return true;
    }
};
} // namespace jkgm
