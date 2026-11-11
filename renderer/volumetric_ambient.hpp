#pragma once
// Sector-local CPU-built 2x2x2 volumetric ambient; GPU sampling in
// gpu_3do_preview.vert, portal selection reused from world_vertex_ambient.hpp.
// Build once per world snapshot from authored Gouraud vertex intensities.
// Eight uint8 corner samples per sector; runtime ExtraLight is NOT baked.
#include "world_cache.hpp"
#include "light_intensity_range.hpp"
#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <limits>
#include <unordered_map>
#include <vector>

namespace jkgm {

struct volumetric_ambient_stats {
    uint32_t surfaces_seen=0, surfaces_sampled=0, surfaces_skipped=0;
    uint32_t vertices_sampled=0, sectors=0;
};

// A single 2x2x2 sector-local field. Index bits are x=1, y=2, z=4.
// The predefined quantization range is [0,1] or [0,2], never an
// automatically varying exposure that could shift lighting between levels.
struct volumetric_ambient_sector {
    uint32_t id=0;
    uintptr_t native_sector=0;
    std::array<float,3> lo{},hi{};
    std::array<uint8_t,8> light{};
    float range=1.0f;

    float sample(std::array<float,3> const &position) const {
        std::array<float,3> t{};
        for(unsigned d=0;d<3;++d) {
            float const span=hi[d]-lo[d];
            t[d]=span>0.000001f
                ? std::clamp((position[d]-lo[d])/span,0.0f,1.0f)
                : 0.5f;
        }
        // Ordinary trilinear interpolation, suitable for the vertex shader
        // in Batch B. The same address order can be uploaded unchanged.
        float brightness=0.0f;
        for(unsigned corner=0;corner<8;++corner) {
            float weight=1.0f;
            for(unsigned axis=0;axis<3;++axis)
                weight*=((corner>>axis)&1U)?t[axis]:1.0f-t[axis];
            brightness+=weight*(float(light[corner])/255.0f)*range;
        }
        return brightness;
    }
};

class volumetric_ambient_field {
    struct source {
        std::array<float,3> position{};
        float brightness=0.0f;
    };
    struct sector_samples {
        uintptr_t native_sector=0;
        std::vector<source> points;
        std::array<float,3> lo{{
            std::numeric_limits<float>::max(),
            std::numeric_limits<float>::max(),
            std::numeric_limits<float>::max()}};
        std::array<float,3> hi{{
            std::numeric_limits<float>::lowest(),
            std::numeric_limits<float>::lowest(),
            std::numeric_limits<float>::lowest()}};
    };
    std::unordered_map<uint32_t,sector_samples> building_;
    std::vector<volumetric_ambient_sector> sectors_;
    uint32_t source_count_=0;
    bool ready_=false;

public:
    void reset() {
        building_.clear();
        sectors_.clear();
        source_count_=0;
        ready_=false;
    }
    uint32_t source_count() const {return source_count_;}
    size_t sector_count() const {return sectors_.size();}
    bool ready() const {return ready_;}
    std::vector<volumetric_ambient_sector> const &sectors() const {
        return sectors_;
    }

    bool add(uint32_t id,uintptr_t sector,
             std::array<float,3> const &position,float intensity,
             bool overbright) {
        if(ready_ || source_count_>=1000000U ||
           !decal_native_address_ok(sector) || !std::isfinite(intensity))
            return false;
        for(float coord:position)
            if(!std::isfinite(coord))return false;
        auto [it,inserted]=building_.try_emplace(id);
        auto &entry=it->second;
        if(inserted)entry.native_sector=sector;
        else if(entry.native_sector!=sector)return false;
        for(unsigned d=0;d<3;++d) {
            entry.lo[d]=std::min(entry.lo[d],position[d]);
            entry.hi[d]=std::max(entry.hi[d],position[d]);
        }
        entry.points.push_back({position,clamp_authored_light(intensity,overbright)});
        ++source_count_;
        return true;
    }

    // Deterministic output order and only 8 bytes of lighting per sector.
    // Corner values are low-frequency inverse-distance weighted averages
    // of world vertex samples, calculated just once during world loading.
    // Portal/sector selection and live ExtraLight remain separate in Batch B.
    bool finalize(bool overbright) {
        if(ready_)return false;
        float const range=authored_light_limit(overbright);
        sectors_.reserve(building_.size());
        for(auto const &pair:building_) {
            auto const &input=pair.second;
            if(input.points.empty())continue;
            volumetric_ambient_sector out{};
            out.id=pair.first;
            out.native_sector=input.native_sector;
            out.lo=input.lo;
            out.hi=input.hi;
            out.range=range;
            for(unsigned corner=0;corner<8;++corner) {
                std::array<double,3> pos{};
                for(unsigned axis=0;axis<3;++axis)
                    pos[axis]=(corner>>axis)&1U
                        ? input.hi[axis]:input.lo[axis];
                double weighted=0.0,weights=0.0;
                for(auto const &point:input.points) {
                    double dist2=0.0;
                    for(unsigned d=0;d<3;++d) {
                        double const diff=double(point.position[d])-pos[d];
                        dist2+=diff*diff;
                    }
                    double const w=1.0/std::max(0.0001,dist2);
                    weighted+=w*point.brightness;
                    weights+=w;
                }
                double const avg=weights>0.0?weighted/weights:0.0;
                out.light[corner]=static_cast<uint8_t>(std::lround(
                    std::clamp(avg/double(range),0.0,1.0)*255.0));
            }
            sectors_.push_back(out);
        }
        std::sort(sectors_.begin(),sectors_.end(),
            [](auto const &a,auto const &b){return a.id<b.id;});
        building_.clear(); // Startup-only source samples are no longer needed.
        ready_=true;
        return true;
    }

    bool sample(uint32_t sector,std::array<float,3> const &position,
                float &brightness) const {
        if(!ready_)return false;
        for(float v:position)if(!std::isfinite(v))return false;
        auto const found=std::lower_bound(sectors_.begin(),sectors_.end(),sector,
            [](auto const &entry,uint32_t id){return entry.id<id;});
        if(found==sectors_.end() || found->id!=sector)return false;
        brightness=found->sample(position);
        return true;
    }
};

// Native world-face capture: sector membership is explicit. Do NOT add
// SithSector.ambientLight, SithSector.extraLight or per-face ExtraLight.
// The former is Thing-only; the latter two are live/floor contributions that
// must not become permanently baked into the generated ambient volume.
// Existing GPU Gouraud rendering is untouched by this CPU-only prototype.
template<class Read>
bool capture_volumetric_ambient(Read const &read,uintptr_t world,
    uintptr_t surfaces,world_cache_mesh const &mesh,bool overbright,
    volumetric_ambient_field &field,volumetric_ambient_stats &stats)
{
    field.reset();
    stats={};
    uint32_t count=0,base=0;
    if(!decal_native_address_ok(world) ||
       !decal_native_address_ok(surfaces) ||
       !read(world+0x4cU,&count,4U) ||
       !read(world+0x50U,&base,4U) ||
       !count || count>65536U || !decal_native_address_ok(base) ||
       uint64_t(base)+uint64_t(count)*0x98U>=0x7ff00000U)
        return false;
    uint64_t const sector_end=uint64_t(base)+uint64_t(count)*0x98U;
    for(auto const &range:mesh.surfaces) {
        ++stats.surfaces_seen;
        if((range.surface_flags&0x600U)!=0U || range.passable_adjoin ||
           range.count<3U || range.count%3U!=0U ||
           uint64_t(range.first_index)+range.count>mesh.indices.size()) {
            ++stats.surfaces_skipped;continue;
        }
        uint32_t const n=range.count/3U+2U;
        uint32_t const first=mesh.indices[range.first_index];
        if(n>128U || uint64_t(first)+n>mesh.vertices.size() ||
           uint64_t(surfaces)+uint64_t(range.surface_index)*0x5cU>=
               0x7ff00000U) {
            ++stats.surfaces_skipped;continue;
        }
        uintptr_t const face=surfaces+uintptr_t(range.surface_index)*0x5cU;
        uint32_t sector=0,geo=0,mode=0,static_ptr=0;
        if(!read(face+0x08U,&sector,4U) ||
           !read(face+0x1cU,&geo,4U) ||
           !read(face+0x20U,&mode,4U) ||
           !read(face+0x54U,&static_ptr,4U) ||
           uint64_t(sector)<base || uint64_t(sector)>=sector_end ||
           (sector-base)%0x98U!=0U || geo<3U || mode<3U) {
            ++stats.surfaces_skipped;continue;
        }
        std::array<float,128> lights{};
        if(static_ptr &&
           (!decal_native_address_ok(static_ptr) ||
            !read(static_ptr,lights.data(),size_t(n)*sizeof(float)))) {
            ++stats.surfaces_skipped;continue;
        }
        uint32_t id=0;
        if(!read(sector,&id,4U)) {
            ++stats.surfaces_skipped;continue;
        }
        bool accepted=true;
        for(unsigned i=0;i<n;++i)
            if(!std::isfinite(lights[i]) ||
               !field.add(id,sector,mesh.vertices[first+i].position,
                          lights[i],overbright)) {
                accepted=false;break;
            }
        if(accepted) {
            ++stats.surfaces_sampled;
            stats.vertices_sampled+=n;
        } else ++stats.surfaces_skipped;
    }
    if(!field.finalize(overbright))return false;
    stats.sectors=static_cast<uint32_t>(field.sector_count());
    return true;
}

} // namespace jkgm
