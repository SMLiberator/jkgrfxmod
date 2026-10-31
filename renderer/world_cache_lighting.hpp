#pragma once
// Live JK world lighting; static world geometry remains immutable.
// RGB output intentionally reserves a MotS-style three-channel light stream.
#include "world_cache_visibility.hpp"
#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <vector>

namespace jkgm {
struct world_cache_light_sample {
    uint32_t mode=0,vertices=0;
};

// SithSurface: +0x08 sector; +0x20 rdFace lightingMode;
// +0x2c vertex positions; +0x44 rdFace extraLight;
// +0x58 per-surface vertex intensities. SithWorld: +0x98 dynamic lights.
// Any unreadable lighting source fails closed so the native face is visible.
inline bool sample_world_cache_surface_light(
    decal_native_read const &read,uintptr_t surfaces,
    world_cache_mesh const &mesh,world_cache_draw_candidate const &face,
    uint32_t native_vertex_count,
    std::vector<float> const &native_dynamic_light,
    std::vector<std::array<float,3>> &out,
    world_cache_light_sample *sample=nullptr,
    float camera_ambient=0.0f)
{
    if(!read || face.count<3U || face.count%3U!=0U ||
       uint64_t(face.first_index)+face.count>mesh.indices.size())
        return false;
    uint32_t const count=face.count/3U+2U;
    if(count>128U)return false;
    uint32_t const base=mesh.indices[face.first_index];
    if(uint64_t(base)+count>mesh.vertices.size() ||
       uint64_t(base)+count>out.size())return false;
    uint64_t const addr64=uint64_t(surfaces)+
                          uint64_t(face.surface_index)*0x5cU;
    if(addr64>0x7ff00000U)return false;
    uintptr_t const addr=static_cast<uintptr_t>(addr64);
    uint32_t mode=0;
    if(!read_decal_u32(read,addr+0x20U,mode) || mode>3U)return false;
    if(sample) {*sample={mode,count};}
    if(mode==0U) {
        for(uint32_t i=0;i<count;++i)out[base+i]={{1.0f,1.0f,1.0f}};
        return true;
    }
    uint32_t sector=0;
    float extra=0.0f,ambient=0.0f;
    if(!read_decal_u32(read,addr+0x08U,sector) ||
       !decal_native_address_ok(sector) ||
       !read(addr+0x44U,&extra,sizeof(extra)) ||
       !read(uintptr_t(sector)+0x08U,&ambient,sizeof(ambient)) ||
       !std::isfinite(extra) || !std::isfinite(ambient))return false;
    // Match the native ambient floor: JKL sector extraLight and the
    // active rdCamera's ambient light both apply to world geometry.
    if(!std::isfinite(camera_ambient))return false;
    ambient=std::max(ambient,camera_ambient);
    auto clamp01=[](float v) {
        return std::max(0.0f,std::min(v,1.0f));
    };
    if(mode!=3U) {
        // 0 fully lit; 1 unlit (face extra/ambient); 2 diffuse (JK's
        // default procEntry static light is 1.0); 3 vertex Gouraud.
        float const bright=mode==1U?
            std::max(clamp01(extra),clamp01(ambient)):1.0f;
        float const gamma=std::pow(bright,1.8f);
        for(uint32_t i=0;i<count;++i)
            out[base+i]={{gamma,gamma,gamma}};
        return true;
    }
    if(native_vertex_count==0U ||
       native_dynamic_light.size()!=native_vertex_count)return false;
    uint32_t indices=0,static_ptr=0;
    if(!read_decal_u32(read,addr+0x2cU,indices) ||
       !read_decal_u32(read,addr+0x58U,static_ptr) ||
       !decal_native_address_ok(indices))return false;
    std::array<uint32_t,128> ids{};
    std::array<float,128> source{};
    if(!read(indices,ids.data(),count*sizeof(uint32_t)))return false;
    if(static_ptr!=0U &&
       (!decal_native_address_ok(static_ptr) ||
        !read(static_ptr,source.data(),count*sizeof(float))))return false;
    std::array<std::array<float,3>,128> result{};
    for(uint32_t i=0;i<count;++i) {
        if(ids[i]>=native_vertex_count ||
           !std::isfinite(source[i]) ||
           !std::isfinite(native_dynamic_light[ids[i]]))return false;
        float const brightness=std::max(
            clamp01(clamp01(source[i]+native_dynamic_light[ids[i]])+extra),
            clamp01(ambient));
        float const gamma=std::pow(brightness,1.8f);
        result[i]={{gamma,gamma,gamma}};
    }
    for(uint32_t i=0;i<count;++i)out[base+i]=result[i];
    return true;
}

// One bounded bulk copy per frame, independent of the immutable VBO.
inline bool read_world_cache_dynamic_lights(
    decal_native_read const &read,uintptr_t world,
    uint32_t expected_count,std::vector<float> &out)
{
    out.clear();
    if(!read || !decal_native_address_ok(world) ||
       expected_count==0U || expected_count>1000000U)return false;
    uint32_t count=0,source=0;
    if(!read_decal_u32(read,world+0x88U,count) ||
       count!=expected_count ||
       !read_decal_u32(read,world+0x98U,source) ||
       !decal_native_address_ok(source))return false;
    out.resize(count);
    if(!read(source,out.data(),size_t(count)*sizeof(float))) {
        out.clear();return false;
    }
    return true;
}
} // namespace jkgm
