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
#include <unordered_map>

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


    // Clipped world/3DO triangles keep exact projector coordinates. Use
    // those coordinates to average normals across neighboring receiver
    // polygons: face-constant opacity otherwise creates visible hard seams
    // at model edges. This only runs while building a decal, never per frame.
    struct decal_corner_key {
        int64_t x=0,y=0,z=0;
        bool operator==(decal_corner_key const &b) const {
            return x==b.x && y==b.y && z==b.z;
        }
    };
    struct decal_corner_hash {
        size_t operator()(decal_corner_key const &k) const {
            uint64_t h=14695981039346656037ULL;
            for(int64_t v:{k.x,k.y,k.z}) {
                h^=static_cast<uint64_t>(v);
                h*=1099511628211ULL;
            }
            return static_cast<size_t>(h);
        }
    };
    inline decal_corner_key decal_projected_corner(
        decal_vertex const &v,decal_projector const &p)
    {
        // Quantization is in normalized projector space, so a small bullet
        // hole and a large explosion share the same adjacency tolerance.
        return {static_cast<int64_t>(std::llround(v.u/p.width*65536.0)),
                static_cast<int64_t>(std::llround(v.v/p.height*65536.0)),
                static_cast<int64_t>(std::llround(v.depth/p.depth*65536.0))};
    }
    inline float decal_smooth_opacity(float low,float high,float x)
    {
        float const t=std::clamp((x-low)/(high-low),0.0f,1.0f);
        return t*t*(3.0f-2.0f*t);
    }

    inline bool make_sprite_decal_gpu_mesh(
        decal_world_scan const &scan,decal_projector const &projector,
        uintptr_t material,sprite_decal_texture_coords const &texture,
        float opacity,sprite_decal_mesh &result,
        bool wrap_steep_faces=false,
        bool allow_backfacing_receivers=false)
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
        using averaged_normal=std::array<float,3>;
        std::unordered_map<decal_corner_key,averaged_normal,decal_corner_hash>
            smoothed_normals;
        smoothed_normals.reserve(count);
        // All receiver fragments in this decal share one projector. Fold
        // adjacent surface normals at coincident clipped corners. Keep
        // per-triangle geometry and backface culling exactly as before.
        for(auto const &tri:scan.triangles) {
            auto const &p0=tri.vertices[0].world;
            auto const &p1=tri.vertices[1].world;
            auto const &p2=tri.vertices[2].world;
            decal_vec3 a=decal_sub(p1,p0),b=decal_sub(p2,p0);
            decal_vec3 n{{a[1]*b[2]-a[2]*b[1],
                          a[2]*b[0]-a[0]*b[2],
                          a[0]*b[1]-a[1]*b[0]}};
            float const n2=decal_dot(n,n);
            if(!std::isfinite(n2)||n2<1e-12f)return false;
            float const inv=1.0f/std::sqrt(n2);
            for(float &x:n)x*=inv;
            for(auto const &vertex:tri.vertices) {
                if(!decal_finite(vertex.world) ||
                   !std::isfinite(vertex.u)||!std::isfinite(vertex.v)||
                   !std::isfinite(vertex.depth))return false;
                auto &acc=smoothed_normals[decal_projected_corner(vertex,projector)];
                for(int j=0;j<3;++j)acc[j]+=n[j];
            }
        }
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
                // Projected decals are fully-lit and have no face light
                // anchor requirement. Reuse the three anchor floats for
                // volume UV/depth, interpolated in the fragment shader.
                // Unlike vertex-only fades, this works even when every
                // clipped polygon corner lies on the volume boundary.
                vert.fax=v.u/projector.width+0.5f;
                vert.fay=0.5f-v.v/projector.height;
                vert.faz=v.depth/projector.depth;
                auto const &smooth=smoothed_normals.at(
                    decal_projected_corner(v,projector));
                float const smooth2=decal_dot(smooth,smooth);
                decal_vec3 facing_normal{{normal[0],normal[1],normal[2]}};
                if(smooth2>1e-8f && std::isfinite(smooth2))
                    for(int i=0;i<3;++i)
                        facing_normal[i]=smooth[i]/std::sqrt(smooth2);
                // The old cutoff at facing=-0.25 discarded much of a
                // low-poly actor's arm even when the clipped triangle was
                // within the shallow projector. Moving models fade all
                // the way around the contour; a directly opposite-facing
                // surface still fades to zero, helping prevent bleed-through.
                // Keep the conservative fade for level architecture.
                float const facing=-decal_dot(facing_normal,projector.look);
                vert.face_extra=decal_smooth_opacity(
                    allow_backfacing_receivers?-0.98f:-0.25f,
                    0.50f,facing);
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
