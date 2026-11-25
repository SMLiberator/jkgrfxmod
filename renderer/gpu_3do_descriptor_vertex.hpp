#pragma once
// Format 3: 24-byte primary 3DO vertices and a separate exact RGB8 stream.
// Immutable face normal, anchor, ExtraLight and LightMode are deduplicated
// into an RGBA32F GPU buffer texture (two vec4 texels per descriptor).
#include "gpu_3do_compact_vertex.hpp"
#include <array>
#include <cstdint>
#include <cstring>
#include <map>
#include <vector>

namespace jkgm {
struct gpu_3do_descriptor_vertex {
    float x,y,z;
    uint16_t u,v;
    uint32_t light_normal;
    uint16_t static_light;
    uint16_t descriptor_index;
};
static_assert(sizeof(gpu_3do_descriptor_vertex)==24,
    "Format 3 primary vertex must be 24 bytes");
static_assert(offsetof(gpu_3do_descriptor_vertex,u)==12 &&
              offsetof(gpu_3do_descriptor_vertex,light_normal)==16 &&
              offsetof(gpu_3do_descriptor_vertex,static_light)==20 &&
              offsetof(gpu_3do_descriptor_vertex,descriptor_index)==22,
              "Format 3 vertex attribute offsets changed");

struct gpu_3do_face_descriptor_pack {
    std::vector<gpu_3do_descriptor_vertex> vertices;
    std::vector<uint32_t> colors; // GL_RGBA8, 4 bytes per vertex
    // [0] = normal.xyz, ExtraLight; [1] = face anchor.xyz, LightMode.
    std::vector<std::array<float,4>> texels;
    size_t count() const { return texels.size()/2U; }
    void clear() { vertices.clear();colors.clear();texels.clear(); }
};
enum gpu_3do_descriptor_reject : uint32_t {
    gpu_3do_descriptor_base=1U<<0U,
    gpu_3do_descriptor_uv=1U<<1U,
    gpu_3do_descriptor_light=1U<<2U,
    gpu_3do_descriptor_invalid=1U<<3U,
    gpu_3do_descriptor_capacity=1U<<4U
};

inline bool gpu_3do_pack_descriptor_vertices(
    std::vector<gpu_3do_vertex> const &source,
    gpu_3do_face_descriptor_pack &out,
    uint32_t *rejection_reason=nullptr)
{
    out.clear();
    if(rejection_reason)*rejection_reason=0U;
    std::vector<gpu_3do_packed_vertex> base;
    if(!gpu_3do_pack_vertices(source,base)) {
        if(rejection_reason)*rejection_reason=gpu_3do_descriptor_base;
        return false;
    }
    std::map<std::array<uint32_t,8>,uint16_t> lookup;
    out.vertices.reserve(source.size());
    out.colors.reserve(source.size());
    for(size_t i=0;i<source.size();++i) {
        auto const &v=source[i];
        gpu_3do_descriptor_vertex p{};
        p.x=v.x;p.y=v.y;p.z=v.z;
        p.u=gpu_3do_to_half(v.u);p.v=gpu_3do_to_half(v.v);
        p.light_normal=base[i].light_normal;
        p.static_light=gpu_3do_to_half(v.static_light);
        uint32_t invalid=0U;
        if(!gpu_3do_half_within(v.u,p.u,0.125f) ||
           !gpu_3do_half_within(v.v,p.v,0.125f))
            invalid|=gpu_3do_descriptor_uv;
        if(!gpu_3do_half_within(v.static_light,p.static_light,0.001f))
            invalid|=gpu_3do_descriptor_light;
        std::array<float,8> const face{{
            v.nx,v.ny,v.nz,v.face_extra,v.fax,v.fay,v.faz,v.light_mode}};
        std::array<uint32_t,8> bits{};
        for(size_t k=0;k<face.size();++k) {
            if(!std::isfinite(face[k]))invalid|=gpu_3do_descriptor_invalid;
            std::memcpy(&bits[k],&face[k],sizeof(float));
        }
        if(invalid) {
            if(rejection_reason)*rejection_reason=invalid;
            out.clear();return false;
        }
        auto found=lookup.find(bits);
        if(found==lookup.end()) {
            if(lookup.size()>=65536U) {
                if(rejection_reason)*rejection_reason=gpu_3do_descriptor_capacity;
                out.clear();return false;
            }
            uint16_t const id=static_cast<uint16_t>(lookup.size());
            found=lookup.emplace(bits,id).first;
            out.texels.push_back({{face[0],face[1],face[2],face[3]}});
            out.texels.push_back({{face[4],face[5],face[6],face[7]}});
        }
        p.descriptor_index=found->second;
        out.vertices.push_back(p);
        out.colors.push_back(base[i].rgba);
    }
    return true;
}
} // namespace jkgm
