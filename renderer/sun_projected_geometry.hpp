#pragma once
// CPU-only, bounded hard-shadow polygon projection. The shadow is geometric:
// clip a source triangle along sunlight onto each opaque native-world
// receiving triangle. No shadow textures, depth comparisons or GL context.
// This is an exploratory alternative to directional depth maps.
#include "sprite_decal_world_cache.hpp"
#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <vector>

namespace jkgm {
struct projected_sun_triangle {
    std::array<decal_vec3,3> points{};
    uint32_t source_surface=~0U; // native surface index; ~0 for a 3DO
    decal_vec3 receiver_normal{}; // actual triangle plane, for mask verification
};
inline decal_vec3 projected_sun_cross(decal_vec3 a,decal_vec3 b)
{
    return {{a[1]*b[2]-a[2]*b[1],
             a[2]*b[0]-a[0]*b[2],
             a[0]*b[1]-a[1]*b[0]}};
}
inline decal_vec3 projected_sun_add(decal_vec3 a,decal_vec3 b,float t)
{
    return {{a[0]+b[0]*t,a[1]+b[1]*t,a[2]+b[2]*t}};
}
// Generic Sutherland-Hodgman polygon clip. Signed distances >=0 remain.
// The input is bounded to one caster triangle or a clipped convex polygon.
template<class Distance>
inline void projected_sun_clip(std::vector<decal_vec3> &polygon,
                               Distance const &distance)
{
    if(polygon.empty())return;
    std::vector<decal_vec3> next;
    next.reserve(polygon.size()+2U);
    auto prev=polygon.back();
    float dp=distance(prev);
    for(auto const &curr:polygon) {
        float dc=distance(curr);
        if((dp>=0.0f)!=(dc>=0.0f) && std::fabs(dp-dc)>1e-12f) {
            float t=dp/(dp-dc);
            decal_vec3 q{};
            for(int k=0;k<3;++k)q[k]=prev[k]+(curr[k]-prev[k])*t;
            next.push_back(q);
        }
        if(dc>=0.0f)next.push_back(curr);
        prev=curr;dp=dc;
    }
    polygon.swap(next);
}
// Clip to a convex receiver triangle, after projecting each caster vertex
// onto its plane. Reject negative rays and cap the projection length.
// A receiver triangle is one fan triangle of a native JK world polygon.
inline void project_sun_triangle_on_receiver(
    projected_sun_triangle const &caster,
    std::array<decal_vec3,3> const &receiver,
    decal_vec3 receiver_normal,decal_vec3 ray,float max_distance,
    std::vector<projected_sun_triangle> &out,size_t max_output)
{
    if(out.size()>=max_output || !decal_finite(receiver_normal) ||
       !decal_finite(ray) || !std::isfinite(max_distance) ||
       max_distance<=0.0f)return;
    for(auto const &p:caster.points)if(!decal_finite(p))return;
    for(auto const &p:receiver)if(!decal_finite(p))return;
    // JK surface normals are polygon-wide. Nonplanar quads can contain
    // individual rasterized triangles whose actual planes differ from that
    // shared normal. Project onto the RECEIVER TRIANGLE plane instead.
    auto const face=projected_sun_cross(
        decal_sub(receiver[1],receiver[0]),
        decal_sub(receiver[2],receiver[0]));
    float const len2=decal_dot(face,face);
    float const facing=decal_dot(face,receiver_normal);
    if(!(len2>1.e-12f) || std::fabs(facing)<1.e-9f)return;
    float const scale=(facing>0.0f?1.0f:-1.0f)/std::sqrt(len2);
    decal_vec3 plane_normal{{face[0]*scale,face[1]*scale,face[2]*scale}};
    float const denom=decal_dot(plane_normal,ray);
    if(std::fabs(denom)<0.06f)return; // grazing ray cannot be projected stably
    auto time=[&](decal_vec3 const &p) {
        return decal_dot(decal_sub(receiver[0],p),plane_normal)/denom;
    };
    std::vector<decal_vec3> poly(caster.points.begin(),caster.points.end());
    projected_sun_clip(poly,[&](decal_vec3 const &p){return time(p)-0.005f;});
    projected_sun_clip(poly,[&](decal_vec3 const &p){return max_distance-time(p);});
    if(poly.size()<3U)return;
    for(auto &p:poly)p=projected_sun_add(p,ray,time(p));
    // Receiver winding can oppose its polygon normal, but the clipping
    // half-spaces must consistently use the triangle's actual plane.
    float const side=facing>0.0f ? 1.0f:-1.0f;
    for(int k=0;k<3;++k) {
        decal_vec3 const &a=receiver[k],&b=receiver[(k+1)%3];
        auto const edge=decal_sub(b,a);
        projected_sun_clip(poly,[&](decal_vec3 const &p){
            return side*decal_dot(
                projected_sun_cross(edge,decal_sub(p,a)),plane_normal);
        });
        if(poly.size()<3U)return;
    }
    for(size_t k=1;k+1<poly.size() && out.size()<max_output;++k) {
        auto const area=projected_sun_cross(
            decal_sub(poly[k],poly[0]),decal_sub(poly[k+1],poly[0]));
        if(std::fabs(decal_dot(area,plane_normal))<1e-7f)continue;
        out.push_back({{{poly[0],poly[k],poly[k+1]}},
                       caster.source_surface,plane_normal});
    }
}
// Conservative world-space swept AABB used for the cached native receiver BVH.
inline decal_volume_aabb projected_sun_swept_bounds(
    projected_sun_triangle const &tri,decal_vec3 ray,float distance)
{
    decal_volume_aabb b{};
    for(int k=0;k<3;++k) {
        b.low[k]=std::min({tri.points[0][k],tri.points[1][k],tri.points[2][k]});
        b.high[k]=std::max({tri.points[0][k],tri.points[1][k],tri.points[2][k]});
        b.low[k]+=std::min(0.0f,ray[k]*distance)-0.01f;
        b.high[k]+=std::max(0.0f,ray[k]*distance)+0.01f;
    }
    return b;
}
} // namespace jkgm
