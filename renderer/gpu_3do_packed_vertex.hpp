#pragma once
// GPU-only packed 3DO format. The native rdModel3 geometry remains unmodified.
// Face normal XYZ are UNORM10, W contains LightMode 0..3. Nonstandard modes
// (including MotS modes 4..6) and unrepresentable geometry use float80.
#include "gpu_3do_preview.hpp"
#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <vector>

namespace jkgm {
struct gpu_3do_packed_vertex {
    float x,y,z;           // offset 0: mesh position
    float u,v;             // offset 12: texture pixel UVs
    uint32_t normal_mode;  // offset 20: UNORM10 xyz + 2-bit light mode
    float static_light;    // offset 24: native vertex lighting
    uint32_t light_normal;// offset 28: UNORM10 xyz
    float fax,fay,faz;     // offset 32: native face anchor
    float face_extra;      // offset 44
    uint32_t rgba;         // offset 48: RGB8 tint + reserved blend weight
};
static_assert(sizeof(gpu_3do_packed_vertex)==52,
              "Unexpected packed 3DO vertex stride");
static_assert(offsetof(gpu_3do_packed_vertex,normal_mode)==20,
              "Packed face normal layout changed");
static_assert(offsetof(gpu_3do_packed_vertex,rgba)==48,
              "Packed RGB layout changed");

inline uint32_t gpu_3do_encode_normal10(float x,float y,float z,
                                         uint32_t mode=0U)
{
    auto channel=[](float v)->uint32_t {
        return static_cast<uint32_t>(std::lround(
            (std::max(-1.0f,std::min(v,1.0f))*0.5f+0.5f)*1023.0f));
    };
    return channel(x)|(channel(y)<<10U)|(channel(z)<<20U)|
           ((mode&3U)<<30U);
}
inline float gpu_3do_decode_normal10_component(uint32_t packed,unsigned shift)
{
    return (float((packed>>shift)&1023U)*(2.0f/1023.0f))-1.0f;
}
inline bool gpu_3do_pack_normal_valid(float x,float y,float z)
{
    if(!std::isfinite(x)||!std::isfinite(y)||!std::isfinite(z))
        return false;
    float length_squared=x*x+y*y+z*z;
    return std::isfinite(length_squared)&&
           std::fabs(length_squared-1.0f)<=0.02f;
}
inline bool gpu_3do_pack_color_valid(float value)
{
    if(!std::isfinite(value)||value<0.0f||value>1.0f)return false;
    // No loss of custom HDR or non-RGB8 tint: those meshes use float80.
    return std::fabs(value-std::round(value*255.0f)/255.0f)<0.0001f;
}
inline uint32_t gpu_3do_encode_rgba8(float r,float g,float b)
{
    auto channel=[](float v)->uint32_t {
        return static_cast<uint32_t>(std::lround(v*255.0f));
    };
    return channel(r)|(channel(g)<<8U)|(channel(b)<<16U);
}
inline bool gpu_3do_pack_vertices(
    std::vector<gpu_3do_vertex> const &source,
    std::vector<gpu_3do_packed_vertex> &out)
{
    out.clear();
    if(source.empty())return false;
    out.reserve(source.size());
    for(auto const &v:source) {
        // Do not silently reassign MotS's specialized modes 4,5,6 to 3.
        // Legacy behavior remains authoritative for unknown/invalid inputs.
        if(!std::isfinite(v.light_mode)||
           v.light_mode<0.0f||v.light_mode>3.0f||
           std::floor(v.light_mode)!=v.light_mode||
           !gpu_3do_pack_normal_valid(v.nx,v.ny,v.nz)||
           !gpu_3do_pack_normal_valid(v.lnx,v.lny,v.lnz)||
           !gpu_3do_pack_color_valid(v.cr)||
           !gpu_3do_pack_color_valid(v.cg)||
           !gpu_3do_pack_color_valid(v.cb)) {
            out.clear();
            return false;
        }
        out.push_back({
            v.x,v.y,v.z,v.u,v.v,
            gpu_3do_encode_normal10(v.nx,v.ny,v.nz,uint32_t(v.light_mode)),
            v.static_light,
            gpu_3do_encode_normal10(v.lnx,v.lny,v.lnz),
            v.fax,v.fay,v.faz,v.face_extra,
            gpu_3do_encode_rgba8(v.cr,v.cg,v.cb)});
    }
    return true;
}
} // namespace jkgm
