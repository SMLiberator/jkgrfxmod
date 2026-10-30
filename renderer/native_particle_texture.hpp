#pragma once
// Turn JK's existing solid-color particle billboards into textured quads
// without changing their positions, sizes, clipping or per-particle CELs.
// The coordinates are JK MAT texel coordinates (half-texel inset), not 0..1.
#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>

namespace jkgm {
struct particle_clip_vertex { float x=0,y=0,z=0; };
struct particle_clip_bounds {
    float min_x=0,max_x=0,min_z=0,max_z=0;
};
struct particle_texture_uv { float u=0,v=0; };

inline bool particle_find_quad_bounds(particle_clip_vertex const* vertices,
                                     size_t count,particle_clip_bounds &out)
{
    if(!vertices || count!=4U)return false;
    out={vertices[0].x,vertices[0].x,vertices[0].z,vertices[0].z};
    for(size_t i=0;i<count;++i) {
        auto const &p=vertices[i];
        if(!std::isfinite(p.x)||!std::isfinite(p.y)||!std::isfinite(p.z))
            return false;
        out.min_x=std::min(out.min_x,p.x);
        out.max_x=std::max(out.max_x,p.x);
        out.min_z=std::min(out.min_z,p.z);
        out.max_z=std::max(out.max_z,p.z);
    }
    return (out.max_x-out.min_x)>1.e-7f &&
           (out.max_z-out.min_z)>1.e-7f;
}

// The native Face3S clipper modifies camera-space XYZ in place. Recover UVs
// from that clipped polygon, rather than stretching the image across its
// new screen-space bounding box. This also handles near-plane clipping.
inline bool particle_texture_clipped_uvs(particle_clip_bounds const &bounds,
                                          particle_clip_vertex const* clipped,
                                          size_t count,unsigned width,
                                          unsigned height,
                                          particle_texture_uv* result)
{
    if(!clipped || !result || count<3U || count>16U ||
       width==0U || width>4096U || height==0U || height>4096U)
        return false;
    float const dx=bounds.max_x-bounds.min_x;
    float const dz=bounds.max_z-bounds.min_z;
    if(!(dx>1.e-7f) || !(dz>1.e-7f) ||
       !std::isfinite(dx) || !std::isfinite(dz))
        return false;
    for(size_t i=0;i<count;++i) {
        auto const &p=clipped[i];
        if(!std::isfinite(p.x)||!std::isfinite(p.y)||!std::isfinite(p.z))
            return false;
        // rdSprite's orientation: bottom-left = (.5,height-.5);
        // top-right = (width-.5,.5).
        float const u=(p.x-bounds.min_x)/dx;
        float const v=(bounds.max_z-p.z)/dz;
        result[i]={0.5f+u*float(width-1U),
                   0.5f+v*float(height-1U)};
        if(!std::isfinite(result[i].u)||!std::isfinite(result[i].v))
            return false;
    }
    return true;
}
}
