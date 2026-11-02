#pragma once
// Immutable GPU submission snapshot of one native rdParticle cloud.
// JKGM emits camera-facing quads from a per-Thing vertex list. Reproduce
// those quads in GPU-owned geometry; no rdCache triangle submission needed.
#include "gpu_3do_preview.hpp"
#include <array>
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <memory>
#include <utility>
#include <vector>

namespace jkgm {
struct gpu_particle_dot {
    std::array<float,3> view_position{};
    uintptr_t material=0;
    int32_t cel=-1;
    float radius=0.0f; // rdParticle +0x34 already is a HALF size
    uint32_t light_mode=0U;
    bool textured=false;
    unsigned width=1,height=1;
};
inline std::shared_ptr<gpu_3do_geometry const> make_gpu_particle_cloud(
    std::vector<gpu_particle_dot> const &dots)
{
    if(dots.empty() || dots.size()>256U)return {};
    auto geometry=std::make_shared<gpu_3do_geometry>();
    geometry->vertices.reserve(dots.size()*4U);
    geometry->indices.reserve(dots.size()*6U);
    geometry->groups.reserve(dots.size());
    for(auto const &dot:dots) {
        if(!dot.material || !std::isfinite(dot.radius) ||
           dot.radius<=0.0f || dot.radius>1000.0f ||
           dot.light_mode>3U ||
           (dot.textured && (dot.width==0U || dot.width>4096U ||
                             dot.height==0U || dot.height>4096U)))
            return {};
        for(float v:dot.view_position)if(!std::isfinite(v))return {};
        float const r=dot.radius,wide=r*1.4142135623730951f;
        std::array<std::array<float,2>,4> const point=dot.textured
            ?std::array<std::array<float,2>,4>{{
                {{-r,-r}},{{r,-r}},{{r,r}},{{-r,r}}}}
            :std::array<std::array<float,2>,4>{{
                {{-wide,0}},{{0,-r}},{{wide,0}},{{0,r}}}};
        float const u0=0.5f, v0=0.5f;
        float const u1=float(dot.width)-0.5f;
        float const v1=float(dot.height)-0.5f;
        std::array<std::array<float,2>,4> const uv{{
            {{u0,v1}},{{u1,v1}},{{u1,v0}},{{u0,v0}}
        }};
        gpu_3do_group group{};
        group.offset=static_cast<uint32_t>(geometry->indices.size());
        group.count=6;
        group.face_type=1U;
        group.material=dot.material;
        group.cel=dot.cel;
        group.solid_color=!dot.textured;
        uint32_t const first=static_cast<uint32_t>(geometry->vertices.size());
        for(int k=0;k<4;++k) {
            gpu_3do_vertex v{};
            v.x=dot.view_position[0]+point[k][0];
            v.y=dot.view_position[1];
            v.z=dot.view_position[2]+point[k][1];
            v.u=uv[k][0];v.v=uv[k][1];
            v.ny=v.lny=-1.0f;
            v.fax=dot.view_position[0];
            v.fay=dot.view_position[1];
            v.faz=dot.view_position[2];
            v.cr=v.cg=v.cb=1.0f;
            v.light_mode=dot.light_mode==0U?0.0f:1.0f;
            geometry->vertices.push_back(v);
        }
        group.bounds_min={{dot.view_position[0]-(dot.textured?r:wide),
                           dot.view_position[1],
                           dot.view_position[2]-r}};
        group.bounds_max={{dot.view_position[0]+(dot.textured?r:wide),
                           dot.view_position[1],
                           dot.view_position[2]+r}};
        group.centroid=dot.view_position;
        geometry->indices.insert(geometry->indices.end(),
                                  {first,first+1,first+2,
                                   first,first+2,first+3});
        geometry->groups.push_back(group);
        auto const pair=std::make_pair(group.material,group.cel);
        if(group.solid_color)geometry->solid_material_refs.push_back(pair);
        else geometry->material_refs.push_back(pair);
    }
    auto deduplicate=[](auto &refs) {
        std::sort(refs.begin(),refs.end());
        refs.erase(std::unique(refs.begin(),refs.end()),refs.end());
    };
    deduplicate(geometry->material_refs);
    deduplicate(geometry->solid_material_refs);
    return geometry;
}
} // namespace jkgm
