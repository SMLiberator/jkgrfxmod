#pragma once
// Experimental GL 3.3 3DO vertex format 2: 40 bytes per corner.
// Preserve full float32 positions and exact existing packed normal/RGB
// semantics. Half-float UV, baked light, face extra and face anchor are
// accepted only when reconstruction falls within a conservative tolerance.
// Fallback is per mesh (52-byte, then 80-byte), not per triangle.
#include "gpu_3do_packed_vertex.hpp"
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <limits>
#include <vector>

namespace jkgm {
struct gpu_3do_compact_vertex {
    float x,y,z;                 // offset  0: full precision position
    uint16_t u,v;               // offset 12: GL_HALF_FLOAT original UV
    uint32_t normal_mode;       // offset 16: packed 10:10:10:2
    uint16_t static_light;      // offset 20: GL_HALF_FLOAT
    uint16_t face_extra;        // offset 22: GL_HALF_FLOAT
    uint32_t light_normal;      // offset 24: packed 10:10:10:2
    uint16_t fax,fay,faz;       // offset 28: GL_HALF_FLOAT anchor
    uint16_t reserved=0;        // offset 34: maintain 32-bit alignment
    uint32_t rgba;              // offset 36: original RGB8, no alpha use
};
static_assert(sizeof(gpu_3do_compact_vertex)==40,
    "Compact GPU vertex must be exactly 40 bytes");
static_assert(offsetof(gpu_3do_compact_vertex,u)==12 &&
              offsetof(gpu_3do_compact_vertex,normal_mode)==16 &&
              offsetof(gpu_3do_compact_vertex,static_light)==20 &&
              offsetof(gpu_3do_compact_vertex,light_normal)==24 &&
              offsetof(gpu_3do_compact_vertex,fax)==28 &&
              offsetof(gpu_3do_compact_vertex,rgba)==36,
              "Compact vertex attribute offsets changed");

// Portable IEEE-754 binary32 -> binary16, round to nearest, ties to even.
// Used during mesh upload only, never per frame. GL reads these half bits.
inline uint16_t gpu_3do_to_half(float value)
{
    uint32_t bits=0U;
    std::memcpy(&bits,&value,sizeof(bits));
    uint16_t const sign=uint16_t((bits>>16U)&0x8000U);
    int const exponent=int((bits>>23U)&255U)-127;
    uint32_t mantissa=bits&0x7fffffU;
    if(exponent>15)return uint16_t(sign|0x7c00U);
    if(exponent<-25)return sign;
    if(exponent<-14) {
        mantissa|=0x800000U;
        unsigned const shift=unsigned(-14-exponent)+13U; // 14..24
        uint32_t half_mantissa=mantissa>>shift;
        uint32_t const remainder=mantissa&((uint32_t(1U)<<shift)-1U);
        uint32_t const midway=uint32_t(1U)<<(shift-1U);
        if(remainder>midway ||
           (remainder==midway && (half_mantissa&1U)))
            ++half_mantissa;
        // half_mantissa=1024 naturally becomes min normal (0x0400).
        return uint16_t(sign|uint16_t(half_mantissa));
    }
    uint32_t half_mantissa=mantissa>>13U;
    uint32_t const remainder=mantissa&0x1fffU;
    if(remainder>0x1000U ||
       (remainder==0x1000U && (half_mantissa&1U)))
        ++half_mantissa;
    unsigned half_exponent=unsigned(exponent+15);
    if(half_mantissa>=1024U) {
        half_mantissa=0U;
        ++half_exponent;
    }
    if(half_exponent>=31U)return uint16_t(sign|0x7c00U);
    return uint16_t(sign|uint16_t(half_exponent<<10U)|
                    uint16_t(half_mantissa));
}
inline float gpu_3do_from_half(uint16_t bits)
{
    float const sign=(bits&0x8000U)?-1.0f:1.0f;
    unsigned const exponent=(bits>>10U)&31U;
    unsigned const mantissa=bits&1023U;
    if(exponent==31U)return mantissa?
        std::numeric_limits<float>::quiet_NaN():
        sign*std::numeric_limits<float>::infinity();
    if(exponent==0U)return sign*std::ldexp(float(mantissa),-24);
    return sign*std::ldexp(1.0f+float(mantissa)/1024.0f,
                          int(exponent)-15);
}
inline bool gpu_3do_half_within(float original,
                                uint16_t packed,float tolerance)
{
    return std::isfinite(original) &&
        std::fabs(gpu_3do_from_half(packed)-original)<=tolerance;
}

enum gpu_3do_compact_reject : uint32_t {
    gpu_3do_compact_base=1U<<0U,     // 52-byte normal/mode/RGB not supported
    gpu_3do_compact_uv=1U<<1U,       // half UV pixel precision exceeded
    gpu_3do_compact_light=1U<<2U,    // half baked/extra light exceeded
    gpu_3do_compact_anchor=1U<<3U    // half local face anchor exceeded
};
struct gpu_3do_compact_profile {
    uint64_t accepted_uploads=0, rejected_uploads=0;
    uint64_t base_rejected=0, uv_rejected=0;
    uint64_t light_rejected=0, anchor_rejected=0;
    void record(bool accepted,uint32_t reason) {
        if(accepted) { ++accepted_uploads; return; }
        ++rejected_uploads;
        if(reason&gpu_3do_compact_base)++base_rejected;
        if(reason&gpu_3do_compact_uv)++uv_rejected;
        if(reason&gpu_3do_compact_light)++light_rejected;
        if(reason&gpu_3do_compact_anchor)++anchor_rejected;
    }
};

// These tolerances are explicitly for the format experiment; don't
// quantize geometry position, nor silently saturate large/invalid UVs.
inline bool gpu_3do_pack_compact_vertices(
    std::vector<gpu_3do_vertex> const &source,
    std::vector<gpu_3do_compact_vertex> &out,
    uint32_t *rejection_reason=nullptr)
{
    out.clear();
    if(rejection_reason)*rejection_reason=0U;
    std::vector<gpu_3do_packed_vertex> base;
    if(!gpu_3do_pack_vertices(source,base)) {
        if(rejection_reason)*rejection_reason=gpu_3do_compact_base;
        return false;
    }
    out.reserve(source.size());
    uint32_t rejected=0U;
    for(size_t i=0;i<source.size();++i) {
        auto const &v=source[i];
        auto const &p=base[i];
        gpu_3do_compact_vertex c{};
        c.x=v.x;c.y=v.y;c.z=v.z;
        c.u=gpu_3do_to_half(v.u);c.v=gpu_3do_to_half(v.v);
        c.normal_mode=p.normal_mode;
        c.static_light=gpu_3do_to_half(v.static_light);
        c.face_extra=gpu_3do_to_half(v.face_extra);
        c.light_normal=p.light_normal;
        c.fax=gpu_3do_to_half(v.fax);
        c.fay=gpu_3do_to_half(v.fay);
        c.faz=gpu_3do_to_half(v.faz);
        c.rgba=p.rgba;
        // UV tolerance is in original MAT pixel coordinates. The
        // 256x256 model reference does not guarantee every mesh UV fits
        // in one tile, so enormous/fractional UVs are conservatively rejected.
        if(!gpu_3do_half_within(v.u,c.u,0.125f) ||
           !gpu_3do_half_within(v.v,c.v,0.125f))
            rejected|=gpu_3do_compact_uv;
        if(!gpu_3do_half_within(v.static_light,c.static_light,0.001f) ||
           !gpu_3do_half_within(v.face_extra,c.face_extra,0.001f))
            rejected|=gpu_3do_compact_light;
        if(!gpu_3do_half_within(v.fax,c.fax,0.01f) ||
           !gpu_3do_half_within(v.fay,c.fay,0.01f) ||
           !gpu_3do_half_within(v.faz,c.faz,0.01f))
            rejected|=gpu_3do_compact_anchor;
        if(rejected && !rejection_reason) {
            out.clear();return false;
        }
        if(!rejected)out.push_back(c);
    }
    if(rejected) {
        out.clear();
        if(rejection_reason)*rejection_reason=rejected;
        return false;
    }
    return true;
}
} // namespace jkgm
