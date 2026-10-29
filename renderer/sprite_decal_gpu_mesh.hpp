#pragma once
// Convert world-surface clipped SPR decals to the existing GPU geometry layout.
// This does not enqueue a renderer draw; it is independent of JK memory,
// texture loading and the OpenGL context.
//
// Coordinates remain in WORLD space. A future renderer path uses an identity
// world transform, its current view matrix, the SPR's native MAT pointer
// and a separate full-opacity/alpha-blended decal classification.
#include "sprite_decal_world.hpp"
#include "gpu_3do_preview.hpp"
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <limits>
#include <memory>

namespace jkgm {
    struct sprite_decal_texture_coords {
        // SPR quad corners in original MAT texture pixels. Unlike most
        // game-style UVs, JK uses texel-center coordinates (+0.5).
        float left=0.5f,right=0.5f,top=0.5f,bottom=0.5f;
    };
    struct sprite_decal_mesh {
        std::shared_ptr<gpu_3do_geometry> geometry;
        uintptr_t material=0;
        float opacity=1.0f;
    };

    inline bool valid_sprite_decal_texture_coords(
        sprite_decal_texture_coords const &t)
    {
        return std::isfinite(t.left)&&std::isfinite(t.right)&&
               std::isfinite(t.top)&&std::isfinite(t.bottom)&&
               t.right>t.left && t.bottom>t.top &&
               t.left>=0 && t.top>=0 &&
               t.right<=32768 && t.bottom<=32768;
    }

    inline bool make_sprite_decal_gpu_mesh(
        decal_world_scan const &scan,decal_projector const &projector,
        uintptr_t material,sprite_decal_texture_coords const &texture,
        float opacity,sprite_decal_mesh &result,
        bool wrap_steep_faces=false)
    {
        if(!scan.valid_world || scan.truncated || scan.triangles.empty() ||
           scan.triangles.size()>32768 || !material ||
           !valid_decal_projector(projector) ||
           !valid_sprite_decal_texture_coords(texture) ||
           !std::isfinite(opacity)||opacity<=0||opacity>1)
            return false;
        auto mesh=std::make_shared<gpu_3do_geometry>();
        size_t const count=scan.triangles.size()*3U;
        mesh->vertices.reserve(count);
        mesh->indices.reserve(count);
        mesh->groups.reserve(1);
        mesh->material_refs.emplace_back(material,-1);
        gpu_3do_group group{};
        group.material=material;
        group.cel=-1;
        // Projected receivers are one-sided! The old two-sided flag plus
        // nonplanar fallback disabled raster culling and made top decals
        // visible from underneath thin slabs. Each triangle retains its
        // receiver's original winding; the renderer now culls its back.
        group.face_type=0U;
        group.nonplanar=false;
        group.count=uint32_t(count);
        group.bounds_min={{std::numeric_limits<float>::max(),
                           std::numeric_limits<float>::max(),
                           std::numeric_limits<float>::max()}};
        group.bounds_max={{-std::numeric_limits<float>::max(),
                           -std::numeric_limits<float>::max(),
                           -std::numeric_limits<float>::max()}};
        std::array<double,3> center{{0,0,0}};
        for(auto const &tri:scan.triangles) {
            auto const &p0=tri.vertices[0].world;
            auto const &p1=tri.vertices[1].world;
            auto const &p2=tri.vertices[2].world;
            std::array<float,3> const a{{p1[0]-p0[0],p1[1]-p0[1],p1[2]-p0[2]}};
            std::array<float,3> const b{{p2[0]-p0[0],p2[1]-p0[1],p2[2]-p0[2]}};
            std::array<float,3> normal{{
                a[1]*b[2]-a[2]*b[1],
                a[2]*b[0]-a[0]*b[2],
                a[0]*b[1]-a[1]*b[0]
            }};
            float const n2=decal_dot(normal,normal);
            if(!std::isfinite(n2)||n2<1e-12f)return false;
            float const inv=1.0f/std::sqrt(n2);
            for(float &v:normal)v*=inv;
            for(auto const &v:tri.vertices) {
                if(!decal_finite(v.world) ||
                   !std::isfinite(v.u)||!std::isfinite(v.v)||
                   !std::isfinite(v.depth))return false;
                auto uv=projected_decal_uv(v,projector);
                if(wrap_steep_faces) {
                    // Planar UVs collapse to a line on a face whose normal
                    // is perpendicular to the projector axis. Use projector
                    // depth as the missing tangent coordinate, oriented by
                    // that FACE's normal. The shift is zero at the middle
                    // of the volume, preserving the existing planar mapping
                    // around the impact and avoiding texture scale changes.
                    float const facing=-decal_dot(
                        {{normal[0],normal[1],normal[2]}},projector.look);
                    float const blend=std::clamp(
                        (0.75f-facing)/0.75f,0.0f,1.0f);
                    float const depth_shift=
                        blend*(v.depth-projector.depth*0.5f);
                    uv[0]+=depth_shift*decal_dot(
                        {{normal[0],normal[1],normal[2]}},
                        projector.right)/projector.width;
                    uv[1]-=depth_shift*decal_dot(
                        {{normal[0],normal[1],normal[2]}},
                        projector.up)/projector.height;
                    // Clipped volumes can just touch an outer border;
                    // clamp tiny overshoots instead of rejecting the whole
                    // decal when a steep receiver crosses that border.
                    uv[0]=std::clamp(uv[0],0.0f,1.0f);
                    uv[1]=std::clamp(uv[1],0.0f,1.0f);
                }
                if(!std::isfinite(uv[0])||!std::isfinite(uv[1])||
                   uv[0]<-0.001f||uv[0]>1.001f||
                   uv[1]<-0.001f||uv[1]>1.001f)return false;
                gpu_3do_vertex vert{};
                vert.x=v.world[0];vert.y=v.world[1];vert.z=v.world[2];
                vert.u=texture.left+uv[0]*(texture.right-texture.left);
                vert.v=texture.top+uv[1]*(texture.bottom-texture.top);
                vert.nx=normal[0];vert.ny=normal[1];vert.nz=normal[2];
                vert.lnx=normal[0];vert.lny=normal[1];vert.lnz=normal[2];
                vert.static_light=1.0f;
                vert.light_mode=0.0f; // receiver G-buffer supplies the decal's illumination
                vert.cr=1.0f;vert.cg=1.0f;vert.cb=1.0f;
                vert.fax=p0[0];vert.fay=p0[1];vert.faz=p0[2];
                mesh->vertices.push_back(vert);
                mesh->indices.push_back(uint32_t(mesh->indices.size()));
                for(int axis=0;axis<3;axis++) {
                    group.bounds_min[axis]=std::min(group.bounds_min[axis],v.world[axis]);
                    group.bounds_max[axis]=std::max(group.bounds_max[axis],v.world[axis]);
                    center[axis]+=v.world[axis];
                }
            }
        }
        for(int axis=0;axis<3;axis++)
            group.centroid[axis]=float(center[axis]/double(count));
        mesh->groups.push_back(group);
        result={std::move(mesh),material,opacity};
        return true;
    }
}
