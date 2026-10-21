#pragma once
// JK23 sector-geometry extraction for SPR projected decals.
// Read-only, injectable memory reader. All pointer traversals are bounded;
// nothing in this header writes to the native SithWorld.
// World/surface offsets are reconstructed from JKDF2 and still require
// verification against the actual JK23 executable before runtime use.
#include "sprite_decal_projector.hpp"
#include <algorithm>
#include <cstdint>
#include <cstring>
#include <functional>
#include <limits>
#include <vector>

namespace jkgm {
    using decal_native_read =
        std::function<bool(uintptr_t,void *,size_t)>;

    struct decal_world_triangle {
        std::array<decal_vertex,3> vertices{};
        uint32_t surface_index=0;
    };
    struct decal_world_scan {
        std::vector<decal_world_triangle> triangles;
        uint32_t surfaces_seen=0;
        uint32_t receivers=0;
        uint32_t broadphase_candidates=0; // surfaces requiring precise clipping
        uint32_t invalid_surfaces=0;
        uint32_t skipped_portals=0,skipped_sky=0,skipped_material=0;
        uint32_t skipped_backfaces=0,skipped_projection=0;
        bool truncated=false;
        bool valid_world=false;
    };

    inline bool read_decal_u32(decal_native_read const &read,
                              uintptr_t address,uint32_t &out) {
        return read && read(address,&out,sizeof(out));
    }
    inline bool read_decal_vec3(decal_native_read const &read,
                               uintptr_t address,decal_vec3 &out) {
        return read && read(address,out.data(),sizeof(float)*3) &&
               decal_finite(out);
    }
    inline bool decal_native_address_ok(uintptr_t p) {
        return p>=0x10000U && p<=0x7ff00000U;
    }
    inline decal_world_scan project_decal_world_surfaces(
        decal_native_read const &read,uintptr_t world,
        decal_projector const &projector,
        uint32_t max_output_triangles=2048,
        uint32_t max_surfaces=250000)
    {
        decal_world_scan result;
        if(!read || !decal_native_address_ok(world) ||
           !valid_decal_projector(projector) ||
           max_output_triangles==0 || max_output_triangles>32768 ||
           max_surfaces==0 || max_surfaces>250000)
            return result;

        // SithWorld: world vertex array +0x88/+0x8c; surface array
        // +0xa8/+0xac. The first header fields were verified in JK23.
        uint32_t count_vertices=0,vertices=0;
        uint32_t count_surfaces=0,surfaces=0;
        if(!read_decal_u32(read,world+0x88,count_vertices) ||
           !read_decal_u32(read,world+0x8c,vertices) ||
           !read_decal_u32(read,world+0xa8,count_surfaces) ||
           !read_decal_u32(read,world+0xac,surfaces) ||
           count_vertices==0 || count_vertices>1000000 ||
           count_surfaces>max_surfaces ||
           !decal_native_address_ok(vertices) ||
           !decal_native_address_ok(surfaces))
            return result;

        result.valid_world=true;
        for(uint32_t i=0;i<count_surfaces;++i) {
            ++result.surfaces_seen;
            // SithSurface stride 0x5c; type, portal, flags and rdFace
            // offsets from OpenJKDF2's non-extended JK layout.
            constexpr uintptr_t stride=0x5c;
            uintptr_t const addr=uintptr_t(surfaces)+uintptr_t(i)*stride;
            uint32_t flags=0,adjoin=0,geo_mode=0,material=0;
            uint32_t count=0,indices=0;
            decal_vec3 normal{};
            bool const valid=
                read_decal_u32(read,addr+0x0c,adjoin) &&
                read_decal_u32(read,addr+0x10,flags) &&
                read_decal_u32(read,addr+0x1c,geo_mode) &&
                read_decal_u32(read,addr+0x28,count) &&
                read_decal_u32(read,addr+0x2c,indices) &&
                read_decal_u32(read,addr+0x34,material) &&
                read_decal_vec3(read,addr+0x48,normal);
            if(!valid) {
                ++result.invalid_surfaces;
                continue;
            }
            // Transparent portals, sky surfaces, untextured/empty walls
            // are excluded from this initial opaque-architecture pass.
            if(adjoin) {++result.skipped_portals;continue;}
            if((flags&0x600U)!=0U) {++result.skipped_sky;continue;}
            if(geo_mode<2U || !material || count<3 ||
               count>128U || !decal_native_address_ok(indices)) {
                ++result.skipped_material;continue;
            }
            if(decal_dot(normal,projector.look)>=-0.05f) {
                ++result.skipped_backfaces;continue;
            }
            std::vector<decal_vec3> polygon;
            polygon.reserve(count);
            bool ok=true;
            for(uint32_t v=0;v<count;++v) {
                uint32_t index=0;
                decal_vec3 position{};
                if(!read_decal_u32(read,uintptr_t(indices)+v*4U,index) ||
                   index>=count_vertices ||
                   !read_decal_vec3(read,
                     uintptr_t(vertices)+uintptr_t(index)*12U,position)) {
                    ok=false;break;
                }
                polygon.push_back(position);
            }
            if(!ok) {
                ++result.invalid_surfaces;continue;
            }
            auto clipped=clip_projected_decal(projector,polygon,normal);
            if(clipped.size()<3) {
                ++result.skipped_projection;continue;
            }
            ++result.receivers;
            uint32_t const needed=uint32_t(clipped.size()-2);
            if(needed>max_output_triangles-result.triangles.size()) {
                result.truncated=true;
                break;
            }
            // Apply the same tiny fixed z-fighting offset to every clipped
            // vertex, preserving the UVs calculated on the receiver plane.
            for(auto &v:clipped)
                offset_sprite_decal_receiver(v,normal);
            for(size_t t=1;t+1<clipped.size();++t)
                result.triangles.push_back(
                    {{{clipped[0],clipped[t],clipped[t+1]}},i});
        }
        return result;
    }
}
