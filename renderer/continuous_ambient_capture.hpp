#pragma once
// Native SithWorld capture adapter for the CPU-only continuous field.
// Reuse the validated native passable-Adjoin reader for the GPU field.
#include "continuous_ambient.hpp"
#include "light_intensity_range.hpp"
#include "passable_adjoin_portals.hpp"
#include <array>
#include <cstdint>
#include <cmath>
#include <cstring>

namespace jkgm {
struct continuous_ambient_capture_stats {
    uint32_t surfaces_seen=0,surfaces_sampled=0,surfaces_skipped=0;
    uint32_t vertices_sampled=0,passable_portals=0,triangles_accepted=0;
};

template<class Read>
bool capture_continuous_ambient(Read const &read,uintptr_t world,
    uintptr_t surfaces,world_cache_mesh const &mesh,bool overbright,
    float spacing,continuous_ambient_field &field,
    continuous_ambient_capture_stats &stats)
{
    field.reset(spacing,overbright);
    stats={};
    uint32_t count=0,base=0;
    if(!decal_native_address_ok(world) ||
       !decal_native_address_ok(surfaces) ||
       !read(world+0x4cU,&count,4U) ||
       !read(world+0x50U,&base,4U) ||
       !count || count>65536U || !decal_native_address_ok(base) ||
       uint64_t(base)+uint64_t(count)*0x98U>=0x7ff00000U)
        return false;
    uint64_t const end=uint64_t(base)+uint64_t(count)*0x98U;
    for(auto const &range:mesh.surfaces) {
        ++stats.surfaces_seen;
        if((range.surface_flags&0x600U)!=0U || range.passable_adjoin ||
           range.count<3U || range.count%3U!=0U ||
           uint64_t(range.first_index)+range.count>mesh.indices.size()) {
            ++stats.surfaces_skipped;continue;
        }
        uint32_t const n=range.count/3U+2U;
        uint32_t const first=mesh.indices[range.first_index];
        if(n>128U || uint64_t(first)+n>mesh.vertices.size() ||
           uint64_t(surfaces)+uint64_t(range.surface_index)*0x5cU>=
               0x7ff00000U) {
            ++stats.surfaces_skipped;continue;
        }
        uintptr_t const face=surfaces+uintptr_t(range.surface_index)*0x5cU;
        uint32_t sector=0,geo=0,mode=0,static_ptr=0;
        if(!read(face+0x08U,&sector,4U) ||
           !read(face+0x1cU,&geo,4U) ||
           !read(face+0x20U,&mode,4U) ||
           !read(face+0x54U,&static_ptr,4U) ||
           uint64_t(sector)<base || uint64_t(sector)>=end ||
           (sector-base)%0x98U!=0U || geo<3U || mode<3U) {
            ++stats.surfaces_skipped;continue;
        }
        std::array<float,128> lights{};
        if(static_ptr &&
           (!decal_native_address_ok(static_ptr) ||
            !read(static_ptr,lights.data(),size_t(n)*sizeof(float)))) {
            ++stats.surfaces_skipped;continue;
        }
        uint32_t id=0;
        if(!read(sector,&id,4U)) {
            ++stats.surfaces_skipped;continue;
        }
        bool accepted=true;
        for(unsigned i=0;i<n;++i)
            if(!std::isfinite(lights[i]) ||
               !field.add(id,mesh.vertices[first+i].position,
                          clamp_authored_light(lights[i],overbright))) {
                accepted=false;break;
            }
        if(accepted) {
            ++stats.surfaces_sampled;
            stats.vertices_sampled+=n;
        } else ++stats.surfaces_skipped;
    }
    passable_adjoin_portals portal_geometry;
    portal_geometry.reset();
    stats.passable_portals=capture_passable_adjoin_portals(
        read,world,portal_geometry);
    portal_geometry.for_each_portal_triangle(
        [&](uint32_t a,uint32_t b,
            std::array<std::array<float,3>,3> const &triangle) {
            if(field.add_portal(a,b,triangle))++stats.triangles_accepted;
        });
    return field.finalize();
}
} // namespace jkgm
