#pragma once
// Nearest actual receiver for an unattached projected SPR. A decal spawned
// at an impact position must not assume its Thing UP axis already equals the
// impact normal. Query existing world/static 3DO BVHs, not entire levels.
#include "sprite_decal_world_cache.hpp"
#include <algorithm>
#include <cmath>
#include <limits>

namespace jkgm {
struct decal_nearest_face {
    decal_vec3 normal{};
    float distance=std::numeric_limits<float>::infinity();
    uint32_t surface=0;
    bool valid=false;
};
inline float decal_point_segment_distance2(
    decal_vec3 const &p,decal_vec3 const &a,decal_vec3 const &b)
{
    auto const ab=decal_sub(b,a);
    float const len2=decal_dot(ab,ab);
    float const t=len2>1.e-12f
        ?std::clamp(decal_dot(decal_sub(p,a),ab)/len2,0.0f,1.0f):0.0f;
    decal_vec3 closest{};
    for(int i=0;i<3;++i)closest[i]=a[i]+t*ab[i];
    auto const delta=decal_sub(p,closest);
    return decal_dot(delta,delta);
}
inline float decal_point_polygon_distance2(
    decal_vec3 const &p,decal_world_surface_geometry const &face)
{
    if(face.polygon.size()<3 || !decal_finite(p) ||
       !decal_finite(face.normal))
        return std::numeric_limits<float>::infinity();
    auto const &normal=face.normal;
    float const n2=decal_dot(normal,normal);
    if(!std::isfinite(n2)||n2<0.5f||n2>1.5f)
        return std::numeric_limits<float>::infinity();
    float const signed_dist=decal_dot(
        decal_sub(p,face.polygon[0]),normal)/n2;
    decal_vec3 projected{};
    for(int i=0;i<3;++i)
        projected[i]=p[i]-signed_dist*normal[i];
    int sign=0;
    bool inside=true;
    float nearest=std::numeric_limits<float>::infinity();
    for(size_t i=0;i<face.polygon.size();++i) {
        auto const &a=face.polygon[i];
        auto const &b=face.polygon[(i+1)%face.polygon.size()];
        if(!decal_finite(a)||!decal_finite(b))
            return std::numeric_limits<float>::infinity();
        auto const edge=decal_sub(b,a);
        auto const off=decal_sub(projected,a);
        decal_vec3 cross{{
            edge[1]*off[2]-edge[2]*off[1],
            edge[2]*off[0]-edge[0]*off[2],
            edge[0]*off[1]-edge[1]*off[0]}};
        float const side=decal_dot(cross,normal);
        if(side>1.e-6f) {
            if(sign<0)inside=false;
            sign=1;
        } else if(side<-1.e-6f) {
            if(sign>0)inside=false;
            sign=-1;
        }
        nearest=std::min(nearest,decal_point_segment_distance2(p,a,b));
    }
    return inside ? signed_dist*signed_dist*n2 : nearest;
}
inline void decal_nearest_face_from_geometry(
    decal_world_geometry const &geometry,decal_vec3 const &position,
    decal_vec3 const &incoming,float radius,decal_nearest_face &best)
{
    if(!geometry.valid_world || geometry.receivers.empty() ||
       !decal_finite(position) || !std::isfinite(radius) ||
       radius<=0.0f)return;
    decal_vec3 low{},high{};
    for(int i=0;i<3;++i) {
        low[i]=position[i]-radius;
        high[i]=position[i]+radius;
    }
    std::vector<uint32_t> candidates;
    geometry.acceleration.query(geometry.receivers,low,high,candidates);
    auto const dir2=decal_dot(incoming,incoming);
    bool const have_direction=std::isfinite(dir2)&&dir2>0.5f&&dir2<1.5f;
    for(uint32_t idx:candidates) {
        auto const &face=geometry.receivers[idx];
        float const d2=decal_point_polygon_distance2(position,face);
        if(!std::isfinite(d2)||d2>radius*radius)continue;
        float const d=std::sqrt(d2);
        // On an edge, favor the surface facing an incoming shot when
        // distances are similar. The distance still dominates large gaps.
        float const front=have_direction
            ?std::clamp(-decal_dot(face.normal,incoming),0.0f,1.0f)
            :1.0f;
        float const score=d+(have_direction?(1.0f-front)*0.04f:0.0f);
        if(!best.valid || score<best.distance) {
            best.normal=face.normal;
            best.distance=score;
            best.surface=face.surface_index;
            best.valid=true;
        }
    }
}
inline decal_nearest_face find_nearest_decal_surface(
    decal_world_geometry const &world,
    decal_world_geometry const &objects,
    decal_vec3 const &position,decal_vec3 const &incoming,
    float radius=0.25f)
{
    decal_nearest_face result{};
    decal_nearest_face_from_geometry(
        world,position,incoming,radius,result);
    decal_nearest_face_from_geometry(
        objects,position,incoming,radius,result);
    return result;
}
}
