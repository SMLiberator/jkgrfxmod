#pragma once
// Shared immutable SPR geometry; per-instance size, roll and camera alignment.
#include "gpu_3do_preview.hpp"
#include "gpu_3do_modelview.hpp"
#include "sprite_orientation.hpp"
#include "sprite_decal_gpu_mesh.hpp"
#include <array>
#include <cmath>
#include <memory>
namespace jkgm {
inline std::shared_ptr<gpu_3do_geometry const> make_gpu_sprite_quad(
    uintptr_t mat,sprite_decal_texture_coords const &uv,uint32_t flags,
    bool solid_color=false)
{
    if(!mat || !valid_sprite_decal_texture_coords(uv) || (flags&~3U))return {};
    auto g=std::make_shared<gpu_3do_geometry>();
    gpu_3do_group group{};
    group.material=mat;group.cel=-1;
    group.face_type=flags|1U;group.count=6;
    group.solid_color=solid_color;
    group.bounds_min={{-1,0,-1}};
    group.bounds_max={{1,0,1}};
    g->groups.push_back(group);
    if(solid_color)g->solid_material_refs.emplace_back(mat,-1);
    else g->material_refs.emplace_back(mat,-1);
    constexpr std::array<std::array<float,3>,4> points{{
        {{-1,0,-1}},{{1,0,-1}},{{1,0,1}},{{-1,0,1}}
    }};
    std::array<std::array<float,2>,4> uvpoints{{
        {{uv.left,uv.bottom}},{{uv.right,uv.bottom}},
        {{uv.right,uv.top}},{{uv.left,uv.top}}
    }};
    for(unsigned i=0;i<4;++i) {
        gpu_3do_vertex v{};
        v.x=points[i][0];v.y=points[i][1];v.z=points[i][2];
        v.u=uvpoints[i][0];v.v=uvpoints[i][1];
        v.ny=-1.0f;v.lny=-1.0f;v.static_light=1.0f;
        v.cr=v.cg=v.cb=1.0f;
        g->vertices.push_back(v);
    }
    g->indices={0,1,2,0,2,3};
    return g;
}
// Build a separate light-facing caster, independent of the view camera.
// Reuse the same dimensions/roll/SPR offsets and immutable quad as the
// visible sprite. A fixed SPR also faces the sun in this depth-only pass:
// this approximates volume rather than projecting the card edge-on.
inline bool make_gpu_sprite_sun_shadow_pose(
    gpu_3do_matrix34 const &thing,float const (&size)[4],
    float const (&offset)[3],float width,float height,float roll,
    std::array<float,3> const &sun_direction,
    gpu_3do_matrix34 &out);

// Regular visible sprite remains camera-facing.
inline bool make_gpu_sprite_pose(
    uint32_t type,gpu_3do_matrix34 const &thing,
    gpu_3do_matrix34 const &view,float const (&size)[4],
    float const (&offset)[3],float w,float h,float roll,
    gpu_3do_matrix34 &out)
{
    if(type>sprite_fixed || !gpu_3do_matrix_finite(thing) ||
       !gpu_3do_matrix_finite(view) || !std::isfinite(w) ||
       !std::isfinite(h) || !std::isfinite(roll) ||
       w<=0 || h<=0 || w>1000 || h>1000 ||
       !std::isfinite(size[2]) || !std::isfinite(size[3]) ||
       size[2]<=0 || size[3]<=0 || size[2]>8192 || size[3]>8192)
        return false;
    for(float v:offset)if(!std::isfinite(v))return false;
    sprite_axes object{
        {{thing[0],thing[1],thing[2]}},
        {{thing[3],thing[4],thing[5]}},
        {{thing[6],thing[7],thing[8]}}
    };
    sprite_axes camera{
        {{view[0],view[3],view[6]}},
        {{view[1],view[4],view[7]}},
        {{view[2],view[5],view[8]}}
    },axes{};
    if(!compute_sprite_axes(type,object,camera,axes))return false;
    float const angle=std::fmod(roll,360.0f)*0.01745329251994329577f;
    float const cs=std::cos(angle),sn=std::sin(angle);
    for(int i=0;i<3;++i) {
        out[i]=size[2]*w*(cs*axes.right[i]+sn*axes.up[i]);
        out[i+3]=axes.look[i];
        out[i+6]=size[3]*h*(cs*axes.up[i]-sn*axes.right[i]);
        out[i+9]=thing[i+9]+offset[0]*axes.right[i]+
                 offset[1]*axes.look[i]+offset[2]*axes.up[i];
    }
    return gpu_3do_matrix_finite(out);
}

// The directional ray is normalized once at the caller; validate again
// so pure tests and unusual SetSunInfo values cannot produce NaNs.
inline bool make_gpu_sprite_sun_shadow_pose(
    gpu_3do_matrix34 const &thing,float const (&size)[4],
    float const (&offset)[3],float width,float height,float roll,
    std::array<float,3> const &sun_direction,
    gpu_3do_matrix34 &out)
{
    sprite_vec3 look=sun_direction;
    if(!sprite_normalize(look))return false;
    sprite_vec3 reference=std::fabs(look[2])>0.95f
        ?sprite_vec3{{0,1,0}}:sprite_vec3{{0,0,1}};
    sprite_vec3 right=sprite_cross(look,reference);
    if(!sprite_normalize(right))return false;
    sprite_vec3 up=sprite_cross(right,look);
    if(!sprite_normalize(up))return false;
    gpu_3do_matrix34 sun_view{{
        right[0],look[0],up[0],
        right[1],look[1],up[1],
        right[2],look[2],up[2],
        0,0,0
    }};
    return make_gpu_sprite_pose(
        sprite_default,thing,sun_view,size,offset,width,height,roll,out);
}
}
