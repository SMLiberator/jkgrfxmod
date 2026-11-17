#pragma once
// GPU-only packed 3DO format. The native rdModel3 geometry remains unmodified.
// Face normal XYZ use zero-preserving centered UNORM10, W contains LightMode
// 0..3. Nonstandard modes (including MotS modes 4..6) and values that cannot
// be represented without significant clipping use the original float80 path.
#include "gpu_3do_preview.hpp"
#include <algorithm>
#include <array>
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
    // Bias around 512 instead of 511.5: the all-zero normal remains exactly
    // zero after decoding. Otherwise zero can turn into an arbitrary normal
    // direction, defeating the shader's zero-normal fallback for SSAO.
    auto channel=[](float v)->uint32_t {
        float const limited=std::max(-1.0f,std::min(v,1.0f));
        return static_cast<uint32_t>(std::lround(
            512.0f+(limited<0.0f?limited*512.0f:limited*511.0f)));
    };
    return channel(x)|(channel(y)<<10U)|(channel(z)<<20U)|
           ((mode&3U)<<30U);
}
inline float gpu_3do_decode_normal10_component(uint32_t packed,unsigned shift)
{
    float const centered=float((packed>>shift)&1023U)-512.0f;
    return centered<0.0f?centered/512.0f:centered/511.0f;
}
inline bool gpu_3do_pack_normal_valid(float x,float y,float z)
{
    // Native JK accepts non-unit normals, including zero-length ones.
    // Preserve their magnitude instead of silently normalizing them.
    // Allow one quantization step of harmless rounding overshoot.
    constexpr float margin=1.0f/511.0f;
    return std::isfinite(x)&&std::isfinite(y)&&std::isfinite(z)&&
           std::fabs(x)<=1.0f+margin&&
           std::fabs(y)<=1.0f+margin&&
           std::fabs(z)<=1.0f+margin;
}
inline bool gpu_3do_pack_color_valid(float value)
{
    // Round ordinary 0..1 tints to the nearest RGB8 code; exact multiples
    // of 1/255 are not required. Keep HDR/negative tint in float80 instead.
    return std::isfinite(value)&&value>=0.0f&&value<=1.0f;
}
inline uint32_t gpu_3do_encode_rgba8(float r,float g,float b)
{
    auto channel=[](float v)->uint32_t {
        return static_cast<uint32_t>(std::lround(v*255.0f));
    };
    return channel(r)|(channel(g)<<8U)|(channel(b)<<16U);
}
// Each bit denotes a distinct reason; a rejected mesh may set several bits.
// These counters describe GPU buffer uploads, not unique native model assets.
enum gpu_3do_pack_reject_reason : unsigned {
    gpu_3do_pack_empty=0,
    gpu_3do_pack_mode_nonfinite,
    gpu_3do_pack_mode_out_of_range,
    gpu_3do_pack_mode_fractional,
    gpu_3do_pack_face_normal_nonfinite,
    gpu_3do_pack_face_normal_out_of_range,
    gpu_3do_pack_light_normal_nonfinite,
    gpu_3do_pack_light_normal_out_of_range,
    gpu_3do_pack_rgb_nonfinite,
    gpu_3do_pack_rgb_out_of_range,
    gpu_3do_pack_reject_reason_count
};
inline uint32_t gpu_3do_pack_reason_bit(gpu_3do_pack_reject_reason reason)
{
    return uint32_t(1U)<<unsigned(reason);
}
inline uint32_t gpu_3do_pack_vertex_rejections(gpu_3do_vertex const &v)
{
    uint32_t bits=0;
    auto add=[&](gpu_3do_pack_reject_reason reason) {
        bits|=gpu_3do_pack_reason_bit(reason);
    };
    if(!std::isfinite(v.light_mode))
        add(gpu_3do_pack_mode_nonfinite);
    else if(v.light_mode<0.0f||v.light_mode>3.0f)
        add(gpu_3do_pack_mode_out_of_range);
    else if(std::floor(v.light_mode)!=v.light_mode)
        add(gpu_3do_pack_mode_fractional);
    auto check_normal=[&](float x,float y,float z,
                          gpu_3do_pack_reject_reason nonfinite,
                          gpu_3do_pack_reject_reason out_of_range) {
        if(!std::isfinite(x)||!std::isfinite(y)||!std::isfinite(z))
            add(nonfinite);
        else if(!gpu_3do_pack_normal_valid(x,y,z))
            add(out_of_range);
    };
    check_normal(v.nx,v.ny,v.nz,gpu_3do_pack_face_normal_nonfinite,
                 gpu_3do_pack_face_normal_out_of_range);
    check_normal(v.lnx,v.lny,v.lnz,gpu_3do_pack_light_normal_nonfinite,
                 gpu_3do_pack_light_normal_out_of_range);
    for(float color:{v.cr,v.cg,v.cb}) {
        if(!std::isfinite(color))add(gpu_3do_pack_rgb_nonfinite);
        else if(!gpu_3do_pack_color_valid(color))
            add(gpu_3do_pack_rgb_out_of_range);
    }
    return bits;
}

struct gpu_3do_pack_profile {
    uint64_t accepted_uploads=0,rejected_uploads=0;
    uint64_t packed_vertices=0,legacy_vertices=0;
    std::array<uint64_t,gpu_3do_pack_reject_reason_count> reasons{};
    void record(bool packed,size_t vertex_count,uint32_t rejection_bits) {
        if(packed) {
            ++accepted_uploads;
            packed_vertices+=uint64_t(vertex_count);
        } else {
            ++rejected_uploads;
            legacy_vertices+=uint64_t(vertex_count);
            for(unsigned i=0;i<reasons.size();++i)
                if((rejection_bits&(uint32_t(1U)<<i))!=0)
                    ++reasons[i];
        }
    }
};

inline bool gpu_3do_pack_vertices(
    std::vector<gpu_3do_vertex> const &source,
    std::vector<gpu_3do_packed_vertex> &out,
    uint32_t *rejection_reasons=nullptr)
{
    out.clear();
    if(rejection_reasons)*rejection_reasons=0;
    if(source.empty()) {
        if(rejection_reasons)
            *rejection_reasons=gpu_3do_pack_reason_bit(gpu_3do_pack_empty);
        return false;
    }
    out.reserve(source.size());
    uint32_t reasons=0;
    for(auto const &v:source) {
        // Ordinary uploads still exit on the first bad vertex. Only when
        // profiling is enabled do we scan the rest of a rejected mesh to
        // identify ALL distinct fallback causes (without counting vertices).
        uint32_t const invalid=gpu_3do_pack_vertex_rejections(v);
        if(invalid!=0) {
            reasons|=invalid;
            if(!rejection_reasons) {
                out.clear();
                return false;
            }
        }
        if(reasons!=0)continue;
        out.push_back({
            v.x,v.y,v.z,v.u,v.v,
            gpu_3do_encode_normal10(v.nx,v.ny,v.nz,uint32_t(v.light_mode)),
            v.static_light,
            gpu_3do_encode_normal10(v.lnx,v.lny,v.lnz),
            v.fax,v.fay,v.faz,v.face_extra,
            gpu_3do_encode_rgba8(v.cr,v.cg,v.cb)});
    }
    if(reasons!=0) {
        out.clear();
        if(rejection_reasons)*rejection_reasons=reasons;
        return false;
    }
    return true;
}
} // namespace jkgm
