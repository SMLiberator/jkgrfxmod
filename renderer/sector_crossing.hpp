#pragma once
// Read-only, bounded CPU sector crossing foundation. Sector volume
// behavior follows JK's plane-based sithIntersect_IsSphereInSector.
// Engine pointers come from JKGM's validated SithWorld/SithSector layout;
// OpenJKDF2 is a reference for semantics, never for absolute addresses.
#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <utility>
#include <vector>

namespace jkgm {
using sector_crossing_vec3=std::array<float,3>;
struct sector_crossing_plane {
    sector_crossing_vec3 normal{};
    float offset=0.0f; // dot(normal,world_position)+offset>=0 is inside
};
struct sector_crossing_region {
    uint32_t id=0;
    uintptr_t native_sector=0;
    sector_crossing_vec3 bounds_min{},bounds_max{};
    sector_crossing_vec3 interior_min{},interior_max{};
    bool has_interior_box=false,valid=false;
    std::vector<sector_crossing_plane> planes;
};
struct sector_crossing_snapshot {
    uintptr_t world=0;
    uint32_t native_sector_count=0;
    size_t valid_count=0;
    std::vector<sector_crossing_region> sectors;
};
inline bool sector_crossing_finite(sector_crossing_vec3 const &v) {
    return std::isfinite(v[0]) && std::isfinite(v[1]) && std::isfinite(v[2]);
}
inline bool sector_crossing_valid_box(
    sector_crossing_vec3 const &lo,sector_crossing_vec3 const &hi)
{
    if(!sector_crossing_finite(lo)||!sector_crossing_finite(hi))return false;
    for(int j=0;j<3;++j)if(lo[j]>hi[j])return false;
    return true;
}
inline bool sector_crossing_box_contains(
    sector_crossing_vec3 const &lo,sector_crossing_vec3 const &hi,
    sector_crossing_vec3 const &p,float e=0.0f)
{
    for(int j=0;j<3;++j)
        if(p[j]<lo[j]-e || p[j]>hi[j]+e)return false;
    return true;
}
inline bool sector_crossing_sphere_overlaps_box(
    sector_crossing_vec3 const &origin,float radius,
    sector_crossing_vec3 const &lo,sector_crossing_vec3 const &hi)
{
    if(!sector_crossing_finite(origin) || !std::isfinite(radius) ||
       radius<0.0f)return false;
    double distance_squared=0.0;
    for(int j=0;j<3;++j) {
        double d=origin[j]<lo[j] ? double(lo[j])-origin[j] :
                 (origin[j]>hi[j] ? double(origin[j])-hi[j] : 0.0);
        distance_squared+=d*d;
    }
    return distance_squared<=double(radius)*radius;
}
inline bool sector_crossing_contains(
    sector_crossing_region const &sector,
    sector_crossing_vec3 const &point,float epsilon=0.0001f)
{
    if(!sector.valid || !sector_crossing_finite(point) ||
       !std::isfinite(epsilon) || epsilon<0.0f ||
       !sector_crossing_box_contains(
           sector.bounds_min,sector.bounds_max,point,epsilon))return false;
    // Like native JK, a point strictly inside a collision box needs no
    // face tests. The bounds still must match.
    if(sector.has_interior_box) {
        bool inside=true;
        for(int j=0;j<3;++j)
            if(point[j]<=sector.interior_min[j] ||
               point[j]>=sector.interior_max[j])inside=false;
        if(inside)return true;
    }
    for(auto const &plane:sector.planes) {
        double distance=double(point[0])*plane.normal[0]+
                        double(point[1])*plane.normal[1]+
                        double(point[2])*plane.normal[2]+plane.offset;
        if(distance< -epsilon)return false;
    }
    return !sector.planes.empty();
}
// At most eight nearby sector volumes per Thing. Exceeding the budget
// disables the experimental crossing selection instead of guessing.
struct sector_crossing_candidates {
    static constexpr uint32_t capacity=8;
    std::array<uint32_t,capacity> indices{};
    uint32_t count=0;
    bool overflow=false;
};
inline sector_crossing_candidates sector_crossing_collect_candidates(
    sector_crossing_snapshot const &snapshot,
    sector_crossing_vec3 const &origin,float radius,uint32_t owner_id)
{
    sector_crossing_candidates result{};
    auto add=[&](size_t index) {
        if(result.count>=result.capacity) {
            result.overflow=true;result.count=0;return false;
        }
        result.indices[result.count++]=static_cast<uint32_t>(index);
        return true;
    };
    // Native ownership wins exact boundary ties; IDs need not equal indices.
    for(size_t i=0;i<snapshot.sectors.size();++i) {
        auto const &s=snapshot.sectors[i];
        if(s.valid && s.id==owner_id &&
           sector_crossing_sphere_overlaps_box(
               origin,radius,s.bounds_min,s.bounds_max)) {
            if(!add(i))return result;
            break;
        }
    }
    for(size_t i=0;i<snapshot.sectors.size();++i) {
        auto const &s=snapshot.sectors[i];
        if(!s.valid || s.id==owner_id ||
           !sector_crossing_sphere_overlaps_box(
               origin,radius,s.bounds_min,s.bounds_max))continue;
        if(!add(i))return result;
    }
    return result;
}
inline uint32_t sector_crossing_select(
    sector_crossing_snapshot const &snapshot,
    sector_crossing_candidates const &candidates,
    sector_crossing_vec3 const &point,uint32_t owner_id)
{
    if(candidates.overflow)return owner_id;
    for(uint32_t i=0;i<candidates.count;++i) {
        auto const index=candidates.indices[i];
        if(index<snapshot.sectors.size() &&
           sector_crossing_contains(snapshot.sectors[index],point))
            return snapshot.sectors[index].id;
    }
    return owner_id;
}
// Capture bounding boxes, optional interior box and collision/movement
// boundary planes from a JKGM native SithWorld. No pointers retained in
// shader state; invalid sector geometry fails closed as one whole region.
template<class Read>
bool sector_crossing_capture(Read const &read,uintptr_t world,
                             sector_crossing_snapshot &out)
{
    out={};
    if(world<0x10000U || world>=0x7ff00000U)return false;
    auto read32=[&](uintptr_t address,uint32_t &v) {
        return read(address,&v,sizeof(v));
    };
    uint32_t sectors=0,count=0,vertices=0,vertex_count=0,
             surfaces=0,surface_count=0;
    if(!read32(world+0x4cU,count) ||
       !read32(world+0x50U,sectors) ||
       !read32(world+0x88U,vertex_count) ||
       !read32(world+0x8cU,vertices) ||
       !read32(world+0xa8U,surface_count) ||
       !read32(world+0xacU,surfaces) ||
       !count || count>65536U || vertex_count>1000000U ||
       surface_count>1000000U ||
       sectors<0x10000U || surfaces<0x10000U || vertices<0x10000U ||
       uint64_t(sectors)+uint64_t(count)*0x98U>=0x7ff00000U ||
       uint64_t(surfaces)+uint64_t(surface_count)*0x5cU>=0x7ff00000U ||
       uint64_t(vertices)+uint64_t(vertex_count)*12U>=0x7ff00000U)
        return false;
    out.world=world;
    out.native_sector_count=count;
    out.sectors.reserve(count);
    size_t total_planes=0;
    for(uint32_t i=0;i<count;++i) {
        uintptr_t const address=uintptr_t(sectors)+uintptr_t(i)*0x98U;
        sector_crossing_region region{};
        region.native_sector=address;
        uint32_t flags=0,first_surface=0,num_surfaces=0;
        if(!read32(address,region.id) ||
           !read32(address+0x24U,num_surfaces) ||
           !read32(address+0x28U,first_surface) ||
           !read32(address+0x34U,flags) ||
           !read(address+0x70U,region.bounds_min.data(),12U) ||
           !read(address+0x7cU,region.bounds_max.data(),12U) ||
           !sector_crossing_valid_box(
               region.bounds_min,region.bounds_max) ||
           num_surfaces>surface_count) {
            out.sectors.push_back(std::move(region));continue;
        }
        if((flags&0x1000U)!=0U)
            region.has_interior_box=
                read(address+0x58U,region.interior_min.data(),12U) &&
                read(address+0x64U,region.interior_max.data(),12U) &&
                sector_crossing_valid_box(
                    region.interior_min,region.interior_max);
        bool good=num_surfaces>0U;
        uint64_t const surf_end=uint64_t(surfaces)+
                                  uint64_t(surface_count)*0x5cU;
        if(good && (first_surface<surfaces ||
           uint64_t(first_surface)>=surf_end ||
           (uint64_t(first_surface)-surfaces)%0x5cU!=0U ||
           uint64_t(num_surfaces)>(surf_end-first_surface)/0x5cU))
            good=false;
        if(good)for(uint32_t j=0;j<num_surfaces;++j) {
            uintptr_t const surf=uintptr_t(first_surface)+uintptr_t(j)*0x5cU;
            uint32_t parent=0,adjoin=0,surf_flags=0,index_ptr=0;
            sector_crossing_vec3 normal{};
            if(!read32(surf+0x08U,parent) ||
               !read32(surf+0x0cU,adjoin) ||
               !read32(surf+0x10U,surf_flags) ||
               !read32(surf+0x2cU,index_ptr) ||
               !read(surf+0x48U,normal.data(),12U) ||
               parent!=address || !sector_crossing_finite(normal)) {
                good=false;break;
            }
            bool movement=false;
            if(adjoin) {
                uint32_t adj_flags=0;
                if(adjoin<0x10000U || adjoin>=0x7ff00000U ||
                   !read32(adjoin,adj_flags)) {
                    good=false;break;
                }
                movement=(adj_flags&0x2U)!=0U;
            }
            if((surf_flags&0x4U)==0U && !movement)continue;
            uint32_t index=0;
            sector_crossing_vec3 anchor{};
            if(index_ptr<0x10000U || index_ptr>=0x7ff00000U ||
               !read32(index_ptr,index) || index>=vertex_count ||
               !read(uintptr_t(vertices)+uintptr_t(index)*12U,
                     anchor.data(),12U) ||
               !sector_crossing_finite(anchor)) {
                good=false;break;
            }
            double n2=double(normal[0])*normal[0]+
                      double(normal[1])*normal[1]+
                      double(normal[2])*normal[2];
            if(!std::isfinite(n2) || n2<1.0e-12 ||
               region.planes.size()>=16384U || total_planes>=1000000U) {
                good=false;break;
            }
            double inv=1.0/std::sqrt(n2);
            sector_crossing_plane plane{};
            for(int k=0;k<3;++k)
                plane.normal[k]=static_cast<float>(normal[k]*inv);
            plane.offset=static_cast<float>(
                -(double(anchor[0])*plane.normal[0]+
                  double(anchor[1])*plane.normal[1]+
                  double(anchor[2])*plane.normal[2]));
            if(!std::isfinite(plane.offset)){good=false;break;}
            region.planes.push_back(plane);
            ++total_planes;
        }
        region.valid=good && !region.planes.empty();
        if(!region.valid)region.planes.clear();
        else ++out.valid_count;
        out.sectors.push_back(std::move(region));
    }
    return out.valid_count>0U;
}
} // namespace jkgm
