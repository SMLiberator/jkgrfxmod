#pragma once
// Opt-in, non-rendering phase-0 visible SithWorld GPU mesh cache.
// Native JK execute-buffer rendering stays active. No double-draws,
// no suppression of sector portals, and no change to HOM behavior.
#include "world_cache.hpp"
#include "world_cache_visibility.hpp"
#include "world_cache_portals.hpp"
#include "base/log.hpp"
#include "glutil/buffer.hpp"
#include "glutil/vertex_array.hpp"
#include <cstddef>
#include <cstdint>
#include <chrono>

namespace jkgm {
struct world_cache_gpu {
    gl::vertex_array vao;
    gl::buffer vertices,indices;
    decal_world_geometry world{};
    world_cache_mesh mesh{};
    std::vector<world_cache_draw_candidate> frame_candidates;
    std::vector<uint8_t> portal_surface_membership;
    uintptr_t loaded_world=0;
    uint64_t revision=0;
    bool ready=false;

    world_cache_gpu()
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
        frame_candidates.clear();
        portal_surface_membership.clear();
        // Reuse the verified shadow/decal world reader. Its receiver-only
        // policy intentionally excludes sky, passable adjoins, etc. Those
        // still render via native JK; parity comes before replacement.
        if(!world.load(read,address,250000U,true) ||
           !build_world_cache_mesh(world,mesh)) {
            world.clear();mesh={};
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
        ready=true;
        ++revision;
        LOG_INFO("WORLD CACHE phase0 built world=",address,
                 " surfaces=",mesh.surfaces.size(),
                 " uv_ready=",mesh.uv_ready_surfaces,
                 " missing_uv=",mesh.missing_uv_surfaces,
                 " skipped_portals=",world.skipped_portals,
                 " skipped_sky=",world.skipped_sky,
                 " skipped_material=",world.skipped_material,
                 " invalid_surfaces=",world.invalid_surfaces,
                 " native_surface_count=",world.surface_count,
                 " vertices=",mesh.vertices.size(),
                 " triangles=",mesh.indices.size()/3U,
                 " bytes=",mesh.vertices.size()*sizeof(world_cache_vertex)+
                           mesh.indices.size()*sizeof(uint32_t),
                 " revision=",revision,
                 " build_us=",std::chrono::duration_cast<
                     std::chrono::microseconds>(
                         std::chrono::steady_clock::now()-start).count(),
                 " mode=nonrendering_native_world_unchanged");
        return true;
    }
};
} // namespace jkgm
