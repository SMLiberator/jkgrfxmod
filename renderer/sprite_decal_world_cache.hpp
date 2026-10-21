#pragma once
// Immutable receiver geometry snapshot for world-space SPR decals.
//
// The previous projector scanned SithWorld's entire surface table,
// revalidating and rereading hundreds of polygons for EVERY moving SPR
// pose. Cache the native level's static vertex/face geometry only once.
// Later queries use cheap in-process polygon AABB tests and clipping.
// AABB rejects cannot discard a truly intersecting projector polygon.
#include "sprite_decal_world.hpp"
#include <array>
#include <cmath>
#include <cstdint>
#include <limits>
#include <vector>

namespace jkgm {
    struct decal_world_surface_geometry {
        uint32_t surface_index=0;
        decal_vec3 normal{};
        decal_vec3 bounds_min{},bounds_max{};
        std::vector<decal_vec3> polygon;
    };
    struct decal_world_geometry {
        uintptr_t world=0,vertices_addr=0,surfaces_addr=0;
        uint32_t vertex_count=0,surface_count=0;
        uint32_t invalid_surfaces=0,skipped_portals=0;
        uint32_t skipped_sky=0,skipped_material=0;
        bool valid_world=false;
        std::vector<decal_world_surface_geometry> receivers;

        void clear(){*this={};}

        bool matches(decal_native_read const &read,uintptr_t current) const {
            if(!valid_world || world!=current || !read)return false;
            uint32_t vertices_count=0,vertex_ptr=0;
            uint32_t surfaces_count=0,surface_ptr=0;
            return read_decal_u32(read,current+0x88,vertices_count) &&
                read_decal_u32(read,current+0x8c,vertex_ptr) &&
                read_decal_u32(read,current+0xa8,surfaces_count) &&
                read_decal_u32(read,current+0xac,surface_ptr) &&
                vertex_count==vertices_count && vertices_addr==vertex_ptr &&
                surface_count==surfaces_count && surfaces_addr==surface_ptr;
        }

        bool load(decal_native_read const &read,uintptr_t current,
                  uint32_t max_surfaces=250000) {
            clear();
            if(!read || !decal_native_address_ok(current))return false;
            uint32_t nverts=0,verts=0,nfaces=0,faces=0;
            if(!read_decal_u32(read,current+0x88,nverts) ||
               !read_decal_u32(read,current+0x8c,verts) ||
               !read_decal_u32(read,current+0xa8,nfaces) ||
               !read_decal_u32(read,current+0xac,faces) ||
               nverts==0 || nverts>1000000 ||
               nfaces>max_surfaces || nfaces==0 ||
               !decal_native_address_ok(verts) ||
               !decal_native_address_ok(faces))
                return false;
            world=current;
            vertices_addr=verts;
            surfaces_addr=faces;
            vertex_count=nverts;
            surface_count=nfaces;
            uint32_t total_polygon_vertices=0;
            // Don't accidentally retain gigabytes from malformed models.
            constexpr uint32_t max_polygon_vertices=500000;
            receivers.reserve(std::min<uint32_t>(nfaces,8192U));
            for(uint32_t i=0;i<nfaces;++i) {
                uintptr_t const addr=uintptr_t(faces)+uintptr_t(i)*0x5c;
                uint32_t flags=0,adjoin=0,geo=0,mat=0,count=0,indices=0;
                decal_vec3 normal{};
                if(!read_decal_u32(read,addr+0x0c,adjoin) ||
                   !read_decal_u32(read,addr+0x10,flags) ||
                   !read_decal_u32(read,addr+0x1c,geo) ||
                   !read_decal_u32(read,addr+0x28,count) ||
                   !read_decal_u32(read,addr+0x2c,indices) ||
                   !read_decal_u32(read,addr+0x34,mat) ||
                   !read_decal_vec3(read,addr+0x48,normal)) {
                    ++invalid_surfaces;continue;
                }
                if(adjoin) {++skipped_portals;continue;}
                if(flags&0x600U) {++skipped_sky;continue;}
                if(geo<2 || !mat || count<3 || count>128 ||
                   !decal_native_address_ok(indices)) {
                    ++skipped_material;continue;
                }
                if(count>max_polygon_vertices-total_polygon_vertices) {
                    clear();
                    return false;
                }
                decal_world_surface_geometry surface{};
                surface.surface_index=i;
                surface.normal=normal;
                surface.polygon.reserve(count);
                surface.bounds_min={{std::numeric_limits<float>::max(),
                                     std::numeric_limits<float>::max(),
                                     std::numeric_limits<float>::max()}};
                surface.bounds_max={{-std::numeric_limits<float>::max(),
                                     -std::numeric_limits<float>::max(),
                                     -std::numeric_limits<float>::max()}};
                bool ok=true;
                for(uint32_t j=0;j<count;++j) {
                    uint32_t idx=0;
                    decal_vec3 xyz{};
                    if(!read_decal_u32(read,uintptr_t(indices)+j*4U,idx) ||
                       idx>=nverts ||
                       !read_decal_vec3(read,uintptr_t(verts)+
                                            uintptr_t(idx)*12U,xyz)) {
                        ok=false;break;
                    }
                    for(int axis=0;axis<3;++axis) {
                        surface.bounds_min[axis]=
                            std::min(surface.bounds_min[axis],xyz[axis]);
                        surface.bounds_max[axis]=
                            std::max(surface.bounds_max[axis],xyz[axis]);
                    }
                    surface.polygon.push_back(xyz);
                }
                if(!ok) {
                    ++invalid_surfaces;
                    continue;
                }
                total_polygon_vertices+=count;
                receivers.emplace_back(std::move(surface));
            }
            valid_world=true;
            return true;
        }
    };

    struct decal_volume_aabb {
        decal_vec3 low{},high{};
    };
    inline decal_volume_aabb sprite_decal_volume_aabb(
        decal_projector const &projector) {
        decal_volume_aabb bounds{};
        for(int axis=0;axis<3;++axis) {
            float const center=projector.origin[axis]+
                               projector.look[axis]*projector.depth*0.5f;
            float const extent=
                std::fabs(projector.right[axis])*projector.width*0.5f+
                std::fabs(projector.up[axis])*projector.height*0.5f+
                std::fabs(projector.look[axis])*projector.depth*0.5f;
            bounds.low[axis]=center-extent;
            bounds.high[axis]=center+extent;
        }
        return bounds;
    }
    inline bool sprite_decal_bounds_overlap(
        decal_volume_aabb const &box,
        decal_world_surface_geometry const &surface) {
        for(int i=0;i<3;++i)
            if(box.high[i]<surface.bounds_min[i] ||
               box.low[i]>surface.bounds_max[i])
                return false;
        return true;
    }

    inline decal_world_scan project_cached_decal_world(
        decal_world_geometry const &geometry,
        decal_projector const &projector,uint32_t max_output_triangles=2048)
    {
        decal_world_scan result{};
        if(!geometry.valid_world || !valid_decal_projector(projector) ||
           max_output_triangles==0 || max_output_triangles>32768)
            return result;
        result.valid_world=true;
        result.surfaces_seen=geometry.surface_count;
        result.invalid_surfaces=geometry.invalid_surfaces;
        result.skipped_portals=geometry.skipped_portals;
        result.skipped_sky=geometry.skipped_sky;
        result.skipped_material=geometry.skipped_material;
        decal_volume_aabb const box=sprite_decal_volume_aabb(projector);
        for(auto const &s:geometry.receivers) {
            // Cheap local broadphase before polygon clipping. Only relevant
            // polygons are touched; NO native reads in this inner loop.
            if(!sprite_decal_bounds_overlap(box,s)) {
                ++result.skipped_projection;
                continue;
            }
            if(decal_dot(s.normal,projector.look)>=-0.05f) {
                ++result.skipped_backfaces;
                continue;
            }
            auto clipped=clip_projected_decal(
                projector,s.polygon,s.normal);
            if(clipped.size()<3) {
                ++result.skipped_projection;
                continue;
            }
            auto const triangles=uint32_t(clipped.size()-2);
            if(triangles>max_output_triangles-result.triangles.size()) {
                result.truncated=true;
                break;
            }
            ++result.receivers;
            for(auto &v:clipped)offset_sprite_decal_receiver(v,s.normal);
            for(size_t t=1;t+1<clipped.size();++t)
                result.triangles.push_back(
                    {{{clipped[0],clipped[t],clipped[t+1]}},
                     s.surface_index});
        }
        return result;
    }
}
