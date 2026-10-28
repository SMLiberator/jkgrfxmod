#pragma once
// Quake III-style projected SPR decals: clip arbitrary convex receiver
// polygons against the finite oriented projector volume. No game pointers.
// Runtime receiver discovery and GPU submission are deliberately separate.
#include <algorithm>
#include <array>
#include <cmath>
#include <initializer_list>
#include <vector>

namespace jkgm {
using decal_vec3=std::array<float,3>;
inline decal_vec3 decal_sub(decal_vec3 a,decal_vec3 b) {
    return {{a[0]-b[0],a[1]-b[1],a[2]-b[2]}};
}
inline float decal_dot(decal_vec3 a,decal_vec3 b) {
    return a[0]*b[0]+a[1]*b[1]+a[2]*b[2];
}
inline bool decal_finite(decal_vec3 a) {
    return std::isfinite(a[0])&&std::isfinite(a[1])&&std::isfinite(a[2]);
}
struct decal_projector {
    decal_vec3 origin{},right{{1,0,0}},up{{0,0,1}},look{{0,1,0}};
    float width=1.0f,height=1.0f,depth=0.25f;
};
struct decal_vertex {
    decal_vec3 world{};
    float u=0,v=0,depth=0; // local projector coordinates
};
inline decal_vertex decal_lerp(decal_vertex a,decal_vertex b,float t) {
    t=std::clamp(t,0.0f,1.0f);
    decal_vertex r;
    for(int j=0;j<3;++j)
        r.world[j]=a.world[j]+(b.world[j]-a.world[j])*t;
    r.u=a.u+(b.u-a.u)*t;
    r.v=a.v+(b.v-a.v)*t;
    r.depth=a.depth+(b.depth-a.depth)*t;
    return r;
}
inline float decal_plane_distance(decal_vertex const &v,
                                  decal_projector const &p,int plane) {
    switch(plane) {
    case 0:return v.u+p.width*0.5f;
    case 1:return p.width*0.5f-v.u;
    case 2:return v.v+p.height*0.5f;
    case 3:return p.height*0.5f-v.v;
    case 4:return v.depth;
    default:return p.depth-v.depth;
    }
}
inline bool valid_decal_projector(decal_projector const &p) {
    if(!decal_finite(p.origin)||!decal_finite(p.right)||
       !decal_finite(p.up)||!decal_finite(p.look)||
       !std::isfinite(p.width)||!std::isfinite(p.height)||
       !std::isfinite(p.depth)||
       p.width<=0.0f||p.height<=0.0f||p.depth<=0.0f)
        return false;
    // The billboard axes form an orthonormal projector basis. Silently
    // accepting skewed or degenerate axes corrupts UVs and depth clipping.
    for(auto axis:{p.right,p.up,p.look}) {
        float const len2=decal_dot(axis,axis);
        if(std::fabs(len2-1.0f)>0.01f) return false;
    }
    return std::fabs(decal_dot(p.right,p.up))<0.01f &&
           std::fabs(decal_dot(p.right,p.look))<0.01f &&
           std::fabs(decal_dot(p.up,p.look))<0.01f;
}
inline std::vector<decal_vertex> clip_projected_decal(
    decal_projector const &projector,
    std::vector<decal_vec3> const &receiver,
    decal_vec3 receiver_normal,
    float min_facing=0.05f,bool enforce_facing=true)
{
    // Experimental corner decals may accept both perpendicular and
    // back-facing receivers. Keep the default facing test for other callers.
    // UV-area filtering below still rejects truly edge-on projections.
    if(!valid_decal_projector(projector) ||
       !decal_finite(receiver_normal) || receiver.size()<3 ||
       receiver.size()>128 || !std::isfinite(min_facing) ||
       min_facing<0.0f||min_facing>=1.0f ||
       (enforce_facing &&
        decal_dot(receiver_normal,projector.look)>-min_facing))
        return {};
    std::vector<decal_vertex> clipped;
    clipped.reserve(receiver.size()+12);
    for(auto const &point:receiver) {
        if(!decal_finite(point))return {};
        auto const rel=decal_sub(point,projector.origin);
        clipped.push_back({point,
            decal_dot(rel,projector.right),
            decal_dot(rel,projector.up),
            decal_dot(rel,projector.look)});
    }
    for(int plane=0;plane<6;++plane) {
        if(clipped.size()<3) return {};
        std::vector<decal_vertex> out;
        out.reserve(clipped.size()+4);
        auto previous=clipped.back();
        float prev_distance=decal_plane_distance(previous,projector,plane);
        for(auto const &next:clipped) {
            float const next_distance=decal_plane_distance(next,projector,plane);
            bool const was_inside=prev_distance>=0.0f;
            bool const now_inside=next_distance>=0.0f;
            if(was_inside!=now_inside) {
                float const t=prev_distance/(prev_distance-next_distance);
                out.push_back(decal_lerp(previous,next,t));
            }
            if(now_inside) out.push_back(next);
            previous=next;
            prev_distance=next_distance;
        }
        clipped.swap(out);
        if(clipped.size()>256) return {}; // reject invalid/repeated geometry
    }
    // Sutherland-Hodgman can insert the SAME corner twice when a model
    // triangle intersects two adjacent projector planes, e.g. the two
    // triangles of a square centered under a bullet-hole projector.
    // Those duplicate vertices later create zero-area fan triangles and
    // make the GPU decal mesh builder reject the *entire* projection.
    //
    // Compare all three projector coordinates: unlike comparing UV alone,
    // this preserves distinct depth vertices on a perpendicular face.
    auto const same_vertex=[](decal_vertex const &a,
                              decal_vertex const &b) {
        constexpr float epsilon=1.e-5f;
        return std::fabs(a.u-b.u)<=epsilon &&
               std::fabs(a.v-b.v)<=epsilon &&
               std::fabs(a.depth-b.depth)<=epsilon;
    };
    std::vector<decal_vertex> unique;
    unique.reserve(clipped.size());
    for(auto const &v:clipped)
        if(unique.empty() || !same_vertex(unique.back(),v))
            unique.push_back(v);
    if(unique.size()>1 && same_vertex(unique.front(),unique.back()))
        unique.pop_back();
    clipped.swap(unique);
    if(clipped.size()<3)return {};

    // Filter degenerate polygons (UV area approximately zero).
    double area=0;
    for(size_t i=0;i<clipped.size();++i) {
        auto const &a=clipped[i],&b=clipped[(i+1)%clipped.size()];
        area+=double(a.u)*b.v-double(b.u)*a.v;
    }
    if(clipped.size()<3 || std::fabs(area)<1.e-8) return {};
    return clipped;
}
inline std::array<float,2> projected_decal_uv(decal_vertex const &v,
                                              decal_projector const &p) {
    return {{v.u/p.width+0.5f,0.5f-v.v/p.height}};
}
}
