#pragma once
// Live JK world lighting; static world geometry remains immutable.
// RGB output intentionally reserves a MotS-style three-channel light stream.
#include "world_cache_visibility.hpp"
#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <vector>

namespace jkgm {
struct world_cache_light_sample {
    uint32_t mode=0,vertices=0;
};

// GPU world pixel lighting uses the SAME live rdCamera light parameters
// as GPU-cached 3DOs: rdLight.minRadius (+0x28) and camera.attenuationMin
// (+0x45c). The native SithWorld Gouraud path has separate max variants;
// they are intentionally NOT used by our independent per-pixel renderer.
struct world_cache_point_light {
    std::array<float,3> position{};
    float radius=0.0f,intensity=0.0f;
};
// The renderer must never change between stale native per-vertex light
// values and pixel lighting merely because the first light was added.
// JK's world dynamic-light scratch buffer can retain previous-frame data
// when sithRender_BuildDynamicLights() is not called. A validated camera
// with zero lights is still a valid *zero dynamic light* pixel frame.
inline bool world_cache_use_pixel_lights(bool camera_ready)
{
    return camera_ready;
}

inline float world_cache_model_style_pixel_light(
    float distance,float radius,float intensity,float attenuation,
    float incidence)
{
    if(!std::isfinite(distance) || !std::isfinite(radius) ||
       !std::isfinite(intensity) || !std::isfinite(attenuation) ||
       !std::isfinite(incidence) || distance<0.0f ||
       distance>=radius || incidence<=0.0f)
        return 0.0f;
    // Mirror gpu_3do_preview.frag::pixel_jk_diffuse_lighting().
    // World geometry uses a static face normal; models can interpolate
    // their vertex normals. Both use distance and Lambert incidence.
    return (intensity-distance*attenuation)*incidence;
}

inline bool read_world_cache_camera_lights(
    decal_native_read const &read,uintptr_t camera_global,
    std::vector<world_cache_point_light> &out,
    float &ambient,float &attenuation)
{
    out.clear();
    ambient=attenuation=0.0f;
    uint32_t camera=0;
    if(!read || !read_decal_u32(read,camera_global,camera) ||
       !decal_native_address_ok(camera))return false;
    int32_t count=0;
    if(!read(uintptr_t(camera)+0x54U,&ambient,4) ||
       !read(uintptr_t(camera)+0x58U,&count,4) ||
       !read(uintptr_t(camera)+0x45cU,&attenuation,4) ||
       !std::isfinite(ambient) || !std::isfinite(attenuation) ||
       count<0 || count>64 || attenuation<0.0f || attenuation>100.0f)
        return false;
    ambient=std::max(0.0f,std::min(ambient,1.0f));
    for(int32_t i=0;i<count;++i) {
        uint32_t ptr=0;
        if(!read_decal_u32(read,uintptr_t(camera)+0x5cU+
                           uintptr_t(i)*4U,ptr) ||
           !decal_native_address_ok(ptr))return false;
        uint32_t id=0,enabled=0;
        int32_t type=0;
        world_cache_point_light light{};
        if(!read_decal_u32(read,ptr,id) ||
           !read(uintptr_t(ptr)+4U,&type,4) ||
           !read_decal_u32(read,uintptr_t(ptr)+8U,enabled) ||
           !read(uintptr_t(ptr)+0x18U,&light.intensity,4) ||
           !read(uintptr_t(ptr)+0x28U,&light.radius,4) ||
           id>=64 || !std::isfinite(light.intensity) ||
           !std::isfinite(light.radius))return false;
        if(!enabled || light.intensity<=0.0f || light.radius<=0.0f)
            continue;
        if(type<0 || type>2 || out.size()>=16U)return false;
        if(!read(uintptr_t(camera)+0x15cU+uintptr_t(id)*12U,
                 light.position.data(),12U) ||
           !std::isfinite(light.position[0]) ||
           !std::isfinite(light.position[1]) ||
           !std::isfinite(light.position[2]))return false;
        out.push_back(light);
    }
    return true;
}

// SithSurface: +0x08 sector; +0x20 rdFace lightingMode;
// +0x2c vertex positions; +0x44 rdFace extraLight;
// +0x54 per-surface vertex intensities (+0x58 is lastTouchedMs). SithWorld: +0x98 dynamic lights.
// Any unreadable lighting source fails closed so the native face is visible.
inline bool sample_world_cache_surface_light(
    decal_native_read const &read,uintptr_t surfaces,
    world_cache_mesh const &mesh,world_cache_draw_candidate const &face,
    uint32_t native_vertex_count,
    std::vector<float> const &native_dynamic_light,
    std::vector<std::array<float,3>> &out,
    world_cache_light_sample *sample=nullptr,
    std::vector<std::array<float,3>> *pixel_state=nullptr,
    bool pixel_lighting=false,
    float global_extra=0.0f,
    std::vector<std::array<float,3>> *sector_tint=nullptr)
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
    // The owning sector is required for lit faces, and for tint even in
    // FullyLit mode. Read live Extra + colormap + tint in one bounded span:
    // +0x08 Extra, +0x0c colormap pointer, +0x10 tint[3]. That avoids an
    // additional VirtualQuery for every world surface on every frame.
    uint32_t sector=0;
    if(mode!=0U || sector_tint) {
        if(!read_decal_u32(read,addr+0x08U,sector) ||
           !decal_native_address_ok(sector))return false;
    }
    float ambient=0.0f;
    if(sector_tint) {
        if(sector_tint->size()!=out.size())return false;
        std::array<uint8_t,20> sector_state{};
        if(!read(uintptr_t(sector)+0x08U,
                 sector_state.data(),sector_state.size()))return false;
        std::memcpy(&ambient,sector_state.data(),sizeof(ambient));
        std::array<float,3> tint{};
        std::memcpy(tint.data(),sector_state.data()+8U,sizeof(tint));
        for(float &v:tint) {
            if(!std::isfinite(v))return false;
            v=std::max(0.0f,std::min(v,1.0f));
        }
        for(uint32_t i=0;i<count;++i)(*sector_tint)[base+i]=tint;
    }
    if(mode==0U) {
        for(uint32_t i=0;i<count;++i) {
            out[base+i]={{1.0f,1.0f,1.0f}};
            if(pixel_state)(*pixel_state)[base+i]={{1.0f,1.0f,0.0f}};
        }
        return true;
    }
    float extra=0.0f;
    if(!read(addr+0x44U,&extra,sizeof(extra)) ||
       (!sector_tint &&
        !read(uintptr_t(sector)+0x08U,&ambient,sizeof(ambient))) ||
       !std::isfinite(extra) || !std::isfinite(ambient))return false;
    // Native world faces use sector Extra, not the camera ambient value.
    // The camera value changes as JK processes each visible sector.
    // sithRender's global extra light can additionally raise/lower the
    // world floor (sithRender_RenderSectors parity).
    if(!std::isfinite(global_extra))return false;
    ambient=std::max(0.0f,std::min(ambient+global_extra,1.0f));
    auto clamp01=[](float v) {
        return std::max(0.0f,std::min(v,1.0f));
    };
    if(mode!=3U) {
        // 0 fully lit; 1 unlit (face extra/ambient); 2 diffuse (JK's
        // default procEntry static light is 1.0); 3 vertex Gouraud.
        float const bright=mode==1U?
            std::max(clamp01(extra),clamp01(ambient)):1.0f;
        float const gamma=std::pow(bright,1.8f);
        for(uint32_t i=0;i<count;++i) {
            out[base+i]={{gamma,gamma,gamma}};
            if(pixel_state)(*pixel_state)[base+i]={{
                mode==1U?extra:1.0f,clamp01(ambient),float(mode)}};
        }
        return true;
    }
    if(native_vertex_count==0U ||
       (!pixel_lighting &&
        native_dynamic_light.size()!=native_vertex_count))return false;
    if(pixel_state && pixel_state->size()!=out.size())return false;
    uint32_t indices=0,static_ptr=0;
    if(!read_decal_u32(read,addr+0x2cU,indices) ||
       !read_decal_u32(read,addr+0x54U,static_ptr) ||
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
           (!pixel_lighting &&
            !std::isfinite(native_dynamic_light[ids[i]])))return false;
        float const raw_brightness=source[i]+extra;
        float const dynamic=pixel_lighting?0.0f:native_dynamic_light[ids[i]];
        float const brightness=std::max(
            clamp01(raw_brightness+dynamic),clamp01(ambient));
        float const gamma=std::pow(brightness,1.8f);
        result[i]={{gamma,gamma,gamma}};
        if(pixel_state)(*pixel_state)[base+i]={{
            raw_brightness,clamp01(ambient),3.0f}};
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
