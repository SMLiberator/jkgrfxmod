#pragma once
// One-shot mesh-accurate position resolution for moving SPR decals.
// The native collision sphere supplies the approximate hit; an inherited
// projectile Look vector supplies the direction of travel. Project a short
// ray through the CURRENT animated receiver triangles, then use a bounded
// closest-segment fallback for sphere hits whose ray misses the visual mesh.
// This never changes Sith collision, Thing placement or decal depth.
#include "sprite_decal_world_cache.hpp"
#include <algorithm>
#include <array>
#include <cmath>
#include <limits>

namespace jkgm {
struct decal_mesh_contact {
    decal_vec3 position{},normal{};
    float travel=std::numeric_limits<float>::infinity(); // from segment start
    float miss=std::numeric_limits<float>::infinity(); // ray-to-mesh distance
    bool valid=false,ray_hit=false;
};
inline decal_vec3 decal_mesh_add(decal_vec3 a,decal_vec3 b,float scale=1.0f)
{
    for(int i=0;i<3;++i)a[i]+=b[i]*scale;
    return a;
}
inline decal_vec3 decal_mesh_closest_triangle(
    decal_vec3 p,decal_vec3 a,decal_vec3 b,decal_vec3 c)
{
    // Closest point on a 3D triangle, including all edge/vertex regions.
    // Based on the barycentric Voronoi-region construction.
    auto const ab=decal_sub(b,a),ac=decal_sub(c,a),ap=decal_sub(p,a);
    float const d1=decal_dot(ab,ap),d2=decal_dot(ac,ap);
    if(d1<=0.0f && d2<=0.0f)return a;
    auto const bp=decal_sub(p,b);
    float const d3=decal_dot(ab,bp),d4=decal_dot(ac,bp);
    if(d3>=0.0f && d4<=d3)return b;
    float const vc=d1*d4-d3*d2;
    if(vc<=0.0f && d1>=0.0f && d3<=0.0f)
        return decal_mesh_add(a,ab,d1/(d1-d3));
    auto const cp=decal_sub(p,c);
    float const d5=decal_dot(ab,cp),d6=decal_dot(ac,cp);
    if(d6>=0.0f && d5<=d6)return c;
    float const vb=d5*d2-d1*d6;
    if(vb<=0.0f && d2>=0.0f && d6<=0.0f)
        return decal_mesh_add(a,ac,d2/(d2-d6));
    float const va=d3*d6-d5*d4;
    if(va<=0.0f && d4-d3>=0.0f && d5-d6>=0.0f)
        return decal_mesh_add(b,decal_sub(c,b),(d4-d3)/
                                         ((d4-d3)+(d5-d6)));
    float const sum=va+vb+vc;
    if(!(sum>1.e-20f))return a;
    return decal_mesh_add(decal_mesh_add(a,ab,vb/sum),ac,vc/sum);
}
inline void decal_mesh_nearest_segments(
    decal_vec3 p,decal_vec3 q,decal_vec3 a,decal_vec3 b,
    decal_vec3 &on_path,decal_vec3 &on_edge)
{
    // Exact closest pair between two finite segments, including parallel
    // segments and degenerate edges.
    auto const u=decal_sub(q,p),v=decal_sub(b,a),w=decal_sub(p,a);
    float const uu=decal_dot(u,u),vv=decal_dot(v,v);
    float const uv=decal_dot(u,v),uw=decal_dot(u,w),vw=decal_dot(v,w);
    float s=0.0f,t=0.0f;
    if(uu<=1.e-20f) {
        t=vv>1.e-20f?std::clamp(vw/vv,0.0f,1.0f):0.0f;
    } else if(vv<=1.e-20f) {
        s=std::clamp(-uw/uu,0.0f,1.0f);
    } else {
        float const det=uu*vv-uv*uv;
        if(det>1.e-20f)
            s=std::clamp((uv*vw-uw*vv)/det,0.0f,1.0f);
        t=std::clamp((uv*s+vw)/vv,0.0f,1.0f);
        s=std::clamp((uv*t-uw)/uu,0.0f,1.0f);
    }
    on_path=decal_mesh_add(p,u,s);
    on_edge=decal_mesh_add(a,v,t);
}
inline bool decal_mesh_contact_better(
    decal_mesh_contact const &candidate,decal_mesh_contact const &best)
{
    if(!candidate.valid)return false;
    if(!best.valid)return true;
    if(candidate.ray_hit!=best.ray_hit)return candidate.ray_hit;
    if(candidate.ray_hit)return candidate.travel<best.travel;
    if(candidate.miss<best.miss-0.00001f)return true;
    return std::fabs(candidate.miss-best.miss)<0.00001f &&
           candidate.travel<best.travel;
}
inline decal_mesh_contact find_decal_mesh_contact(
    decal_world_geometry const &mesh,decal_vec3 const &sphere_hit,
    decal_vec3 incoming,float behind=0.25f,float ahead=0.25f,
    float max_miss=0.06f)
{
    decal_mesh_contact best{};
    float const d2=decal_dot(incoming,incoming);
    if(!mesh.valid_world || !decal_finite(sphere_hit) ||
       !decal_finite(incoming) || !(d2>1.e-8f) ||
       !std::isfinite(behind) || !std::isfinite(ahead) ||
       !std::isfinite(max_miss) ||
       behind<=0.0f || ahead<=0.0f || max_miss<=0.0f)
        return best;
    float const inv=1.0f/std::sqrt(d2);
    for(float &v:incoming)v*=inv;
    auto const start=decal_mesh_add(sphere_hit,incoming,-behind);
    auto const end=decal_mesh_add(sphere_hit,incoming,ahead);
    float const length=behind+ahead;
    float const max_miss2=max_miss*max_miss;
    for(auto const &face:mesh.receivers) {
        // Only front-facing parts of the actual mesh are valid impact
        // targets. Preserve the renderer's one-sided triangle convention.
        if(face.polygon.size()<3 || !decal_finite(face.normal) ||
           decal_dot(face.normal,incoming)>=-0.01f)continue;
        // Avoid building any acceleration structure at each impact: most
        // individual model meshes are small, and this runs only once.
        for(size_t i=1;i+1<face.polygon.size();++i) {
            auto const &a=face.polygon[0],&b=face.polygon[i],
                       &c=face.polygon[i+1];
            auto const u=decal_sub(b,a),v=decal_sub(c,a);
            decal_vec3 n{{u[1]*v[2]-u[2]*v[1],
                          u[2]*v[0]-u[0]*v[2],
                          u[0]*v[1]-u[1]*v[0]}};
            float const n2=decal_dot(n,n);
            if(!std::isfinite(n2) || n2<=1.e-14f)continue;
            float const nd=decal_dot(n,incoming);
            if(nd>=-1.e-7f)continue;
            float const t=decal_dot(n,decal_sub(a,start))/nd;
            if(t>=0.0f && t<=length &&
               (!best.ray_hit || t<best.travel)) {
                auto const p=decal_mesh_add(start,incoming,t);
                auto const on_tri=decal_mesh_closest_triangle(p,a,b,c);
                auto const delta=decal_sub(p,on_tri);
                if(decal_dot(delta,delta)<=1.e-8f) {
                    best={on_tri,face.normal,t,0.0f,true,true};
                    continue;
                }
            }
            if(best.ray_hit)continue;
            decal_vec3 closest_path{},closest_tri{};
            float closest2=std::numeric_limits<float>::infinity();
            float along=std::numeric_limits<float>::infinity();
            auto consider=[&](decal_vec3 const &path,decal_vec3 const &surface) {
                auto const diff=decal_sub(path,surface);
                float const dist2=decal_dot(diff,diff);
                float const t=decal_dot(decal_sub(path,start),incoming);
                if(dist2<closest2 || (dist2==closest2 && t<along)) {
                    closest2=dist2;along=t;closest_path=path;closest_tri=surface;
                }
            };
            // Endpoints projected onto the triangle interior (or its edges).
            consider(start,decal_mesh_closest_triangle(start,a,b,c));
            consider(end,decal_mesh_closest_triangle(end,a,b,c));
            // And the true closest pair of the ray segment and each edge.
            for(auto const &edge:std::array<std::array<decal_vec3,2>,3>{{
                {{a,b}},{{b,c}},{{c,a}}
            }}) {
                decal_vec3 path{},surface{};
                decal_mesh_nearest_segments(
                    start,end,edge[0],edge[1],path,surface);
                consider(path,surface);
            }
            if(closest2<=max_miss2) {
                decal_mesh_contact candidate{
                    closest_tri,face.normal,along,
                    std::sqrt(std::max(0.0f,closest2)),true,false};
                if(decal_mesh_contact_better(candidate,best))
                    best=candidate;
            }
        }
    }
    return best;
}
}
