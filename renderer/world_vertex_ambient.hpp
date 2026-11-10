#pragma once
// Experimental CPU-only sector-local ambient probes from immutable world
// vertex positions and live-at-capture SithSurface authored Gouraud values.
// No GL resources, no inter-sector diffusion, no per-frame world scan.
#include "world_cache.hpp"
#include "light_intensity_range.hpp"
#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <limits>
#include <initializer_list>
#include <unordered_map>

namespace jkgm {

struct world_vertex_ambient_stats {
    uint32_t surfaces_seen=0, surfaces_sampled=0, surfaces_skipped=0;
    uint32_t vertices_sampled=0;
};

class world_vertex_ambient_grid {
    struct key {
        uint32_t sector=0;
        int32_t x=0,y=0,z=0;
        bool operator==(key const &other) const {
            return sector==other.sector && x==other.x &&
                   y==other.y && z==other.z;
        }
    };
    struct key_hash {
        size_t operator()(key const &k) const noexcept {
            // Unsigned conversion avoids signed-overflow UB for negative cells.
            uint64_t h=14695981039346656037ULL;
            for(uint32_t n:{k.sector,uint32_t(k.x),
                            uint32_t(k.y),uint32_t(k.z)}) {
                h^=uint64_t(n);
                h*=1099511628211ULL;
            }
            return static_cast<size_t>(h);
        }
    };
    struct cell {
        std::array<double,3> position_sum{};
        double light_sum=0.0;
        uint32_t count=0;
    };
    std::unordered_map<key,cell,key_hash> cells_;
    float radius_=1.0f;
    uint32_t samples_=0;

    bool cell_coord(std::array<float,3> const &position,
                    std::array<int32_t,3> &coord) const
    {
        for(size_t i=0;i<3;++i) {
            if(!std::isfinite(position[i]))return false;
            double const n=std::floor(double(position[i])/radius_);
            if(n< -1000000.0 || n>1000000.0)return false;
            coord[i]=static_cast<int32_t>(n);
        }
        return true;
    }

public:
    void reset(float radius=1.0f) {
        cells_.clear();
        samples_=0;
        radius_=std::isfinite(radius) && radius>=0.25f &&
                radius<=4.0f ? radius:1.0f;
    }
    size_t cell_count() const {return cells_.size();}
    uint32_t sample_count() const {return samples_;}
    float radius() const {return radius_;}

    bool add(uint32_t sector,std::array<float,3> const &position,float light) {
        if(!std::isfinite(light) || light<0.0f ||
           samples_==std::numeric_limits<uint32_t>::max())return false;
        std::array<int32_t,3> c{};
        if(!cell_coord(position,c))return false;
        auto &cell=cells_[key{sector,c[0],c[1],c[2]}];
        if(cell.count==std::numeric_limits<uint32_t>::max())return false;
        for(size_t i=0;i<3;++i)cell.position_sum[i]+=position[i];
        cell.light_sum+=light;
        ++cell.count;
        ++samples_;
        return true;
    }

    // Interpolate nearby per-cell averages, not per-triangle copies:
    // bounded work even in densely tessellated sectors. A miss is explicit
    // so the caller can keep the native Thing ambient unchanged.
    bool sample(uint32_t sector,std::array<float,3> const &position,
                float &ambient) const
    {
        if(cells_.empty())return false;
        std::array<int32_t,3> lo{},hi{};
        std::array<float,3> low{},high{};
        for(size_t i=0;i<3;++i) {
            low[i]=position[i]-radius_;
            high[i]=position[i]+radius_;
        }
        if(!cell_coord(low,lo) || !cell_coord(high,hi))return false;
        double const r2=double(radius_)*radius_;
        double weighted=0.0,weight_total=0.0;
        // The cell edge equals the search radius: at most 4x4x4 lookups.
        for(int64_t x=lo[0];x<=hi[0];++x)
            for(int64_t y=lo[1];y<=hi[1];++y)
                for(int64_t z=lo[2];z<=hi[2];++z) {
                    auto const found=cells_.find(key{sector,int32_t(x),
                                                  int32_t(y),int32_t(z)});
                    if(found==cells_.end())continue;
                    auto const &v=found->second;
                    if(v.count==0)continue;
                    double dist2=0.0;
                    for(int i=0;i<3;++i) {
                        double const delta=v.position_sum[i]/v.count-position[i];
                        dist2+=delta*delta;
                    }
                    if(dist2>r2)continue;
                    double const weight=1.0/(dist2+0.04*r2);
                    weighted+=weight*(v.light_sum/v.count);
                    weight_total+=weight;
                }
        if(weight_total<=0.0)return false;
        double const result=weighted/weight_total;
        if(!std::isfinite(result) || result<0.0 ||
           result>std::numeric_limits<float>::max())return false;
        ambient=static_cast<float>(result);
        return true;
    }
};

// The immutable mesh duplicates native world corners per SithSurface. Match
// their per-corner static light arrays by the same consecutive ordering.
// Native pointers are validated and read only during the snapshot build.
template<class Read>
bool capture_world_vertex_ambient(
    Read const &read,uintptr_t world,uintptr_t surfaces,
    world_cache_mesh const &mesh,bool overbright,float radius,
    world_vertex_ambient_grid &grid,world_vertex_ambient_stats &stats)
{
    grid.reset(radius);
    stats={};
    uint32_t sector_count=0,sector_base=0;
    if(!decal_native_address_ok(world) ||
       !decal_native_address_ok(surfaces) ||
       !read(world+0x4cU,&sector_count,4U) ||
       !read(world+0x50U,&sector_base,4U) ||
       !sector_count || sector_count>65536U ||
       !decal_native_address_ok(sector_base) ||
       uint64_t(sector_base)+uint64_t(sector_count)*0x98U>=0x7ff00000U)
        return false;
    uint64_t const sector_end=
        uint64_t(sector_base)+uint64_t(sector_count)*0x98U;
    for(auto const &range:mesh.surfaces) {
        ++stats.surfaces_seen;
        // Sky and passable portals are not meaningful local ambient emitters.
        if((range.surface_flags&0x600U)!=0U || range.passable_adjoin ||
           range.count<3U || range.count%3U!=0U ||
           uint64_t(range.first_index)+range.count>mesh.indices.size()) {
            ++stats.surfaces_skipped;continue;
        }
        uint32_t const n=range.count/3U+2U;
        uint32_t const base=mesh.indices[range.first_index];
        if(n>128U || uint64_t(base)+n>mesh.vertices.size() ||
           uint64_t(surfaces)+uint64_t(range.surface_index)*0x5cU>=
               0x7ff00000U) {
            ++stats.surfaces_skipped;continue;
        }
        uintptr_t const face=surfaces+uintptr_t(range.surface_index)*0x5cU;
        uint32_t sector=0,geo=0,mode=0,static_ptr=0;
        float face_extra=0.0f,sector_extra=0.0f;
        if(!read(face+0x08U,&sector,4U) ||
           !read(face+0x1cU,&geo,4U) ||
           !read(face+0x20U,&mode,4U) ||
           !read(face+0x44U,&face_extra,4U) ||
           !read(face+0x54U,&static_ptr,4U) ||
           uint64_t(sector)<sector_base ||
           uint64_t(sector)>=sector_end ||
           (sector-sector_base)%0x98U!=0U ||
           !read(uintptr_t(sector)+0x08U,&sector_extra,4U) ||
           !std::isfinite(face_extra) || !std::isfinite(sector_extra) ||
           geo<3U || mode<3U) {
            ++stats.surfaces_skipped;continue;
        }
        std::array<float,128> intensity{};
        if(static_ptr!=0U &&
           (!decal_native_address_ok(static_ptr) ||
            !read(static_ptr,intensity.data(),size_t(n)*sizeof(float)))) {
            ++stats.surfaces_skipped;continue;
        }
        bool finite=true;
        for(uint32_t i=0;i<n;++i)
            finite=finite && std::isfinite(intensity[i]);
        if(!finite) {++stats.surfaces_skipped;continue;}
        uint32_t sector_id=0;
        if(!read(sector,&sector_id,4U)) {
            ++stats.surfaces_skipped;continue;
        }
        float const floor=clamp_authored_extra_light(sector_extra,overbright);
        float const extra=clamp_authored_extra_light(face_extra,overbright);
        uint32_t accepted=0;
        for(uint32_t i=0;i<n;++i) {
            float const authored=clamp_authored_light(intensity[i],overbright);
            float const light=std::max(0.0f,std::max(authored+extra,floor));
            if(grid.add(sector_id,mesh.vertices[base+i].position,light))
                ++accepted;
        }
        if(accepted) {
            ++stats.surfaces_sampled;
            stats.vertices_sampled+=accepted;
        } else ++stats.surfaces_skipped;
    }
    return true;
}
} // namespace jkgm
