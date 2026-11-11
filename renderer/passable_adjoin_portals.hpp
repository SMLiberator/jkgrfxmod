#pragma once
// Validated native SithWorld passable-Adjoin polygon capture.
// Shared by continuous ambient; no legacy ambient probes or lights.
#include "world_cache.hpp"
#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <vector>
#include <utility>

namespace jkgm {
class passable_adjoin_portals {
public:
    using point=std::array<float,3>;
private:
    struct portal {
        uint32_t a=0,b=0;
        std::vector<std::array<point,3>> triangles;
    };
    std::vector<portal> portals_;
public:
    void reset() {portals_.clear();}
    size_t portal_count() const {return portals_.size();}
    template<class Visit> void for_each_portal_triangle(Visit &&visit) const {
        for(auto const &p:portals_)
            for(auto const &triangle:p.triangles)
                visit(p.a,p.b,triangle);
    }
    bool add_portal(uint32_t a,uint32_t b,std::vector<point> const &polygon) {
        if(a==b || polygon.size()<3U || polygon.size()>128U ||
           portals_.size()>=8192U)return false;
        for(auto const &p:polygon)
            for(float n:p)if(!std::isfinite(n))return false;
        portal entry{};
        entry.a=a;entry.b=b;
        for(size_t i=1;i+1<polygon.size();++i) {
            auto const &p=polygon[0],&q=polygon[i],&r=polygon[i+1];
            double const ax=double(q[0])-p[0],ay=double(q[1])-p[1],
                         az=double(q[2])-p[2];
            double const bx=double(r[0])-p[0],by=double(r[1])-p[1],
                         bz=double(r[2])-p[2];
            double const cx=ay*bz-az*by,cy=az*bx-ax*bz,cz=ax*by-ay*bx;
            if(cx*cx+cy*cy+cz*cz>1.0e-18)
                entry.triangles.push_back({{p,q,r}});
        }
        if(entry.triangles.empty())return false;
        portals_.push_back(std::move(entry));
        return true;
    }
};

// Invisible, non-MAT passable Adjoins do not appear in the render mesh,
// so capture directly from the native SithWorld surface table.
template<class Read>
uint32_t capture_passable_adjoin_portals(
    Read const &read,uintptr_t world,passable_adjoin_portals &grid)
{
    uint32_t nsectors=0,sectors=0,nvertices=0,vertices=0,
             nsurfaces=0,surfaces=0;
    if(!read(world+0x4cU,&nsectors,4U) ||
       !read(world+0x50U,&sectors,4U) ||
       !read(world+0x88U,&nvertices,4U) ||
       !read(world+0x8cU,&vertices,4U) ||
       !read(world+0xa8U,&nsurfaces,4U) ||
       !read(world+0xacU,&surfaces,4U) ||
       nsectors==0U || nsectors>65536U ||
       nvertices==0U || nvertices>1000000U ||
       nsurfaces>250000U ||
       !decal_native_address_ok(sectors) ||
       !decal_native_address_ok(vertices) ||
       !decal_native_address_ok(surfaces) ||
       uint64_t(sectors)+uint64_t(nsectors)*0x98U>=0x7ff00000U ||
       uint64_t(vertices)+uint64_t(nvertices)*12U>=0x7ff00000U ||
       uint64_t(surfaces)+uint64_t(nsurfaces)*0x5cU>=0x7ff00000U)
        return 0U;
    uint64_t const sector_end=uint64_t(sectors)+uint64_t(nsectors)*0x98U;
    auto valid_sector=[&](uint32_t ptr) {
        return uint64_t(ptr)>=sectors && uint64_t(ptr)<sector_end &&
               (ptr-sectors)%0x98U==0U;
    };
    uint32_t added=0;
    for(uint32_t i=0;i<nsurfaces && grid.portal_count()<8192U;++i) {
        uintptr_t const face=uintptr_t(surfaces)+uintptr_t(i)*0x5cU;
        uint32_t adjoin=0;
        if(!read(face+0x0cU,&adjoin,4U) ||
           !decal_native_address_ok(adjoin))continue;
        uint32_t flags=0,destination=0;
        if(!read(adjoin,&flags,4U) ||
           (flags&0x2U)==0U ||
           !read(uintptr_t(adjoin)+0x04U,&destination,4U) ||
           !valid_sector(destination))continue;
        uint32_t owner=0,face_flags=0,ncorners=0,indices=0;
        if(!read(face+0x08U,&owner,4U) ||
           !valid_sector(owner) || owner==destination ||
           !read(face+0x10U,&face_flags,4U) ||
           (face_flags&0x600U)!=0U ||
           !read(face+0x28U,&ncorners,4U) ||
           !read(face+0x2cU,&indices,4U) ||
           ncorners<3U || ncorners>128U ||
           !decal_native_address_ok(indices) ||
           uint64_t(indices)+uint64_t(ncorners)*4U>=0x7ff00000U)
            continue;
        uint32_t a=0,b=0;
        if(!read(owner,&a,4U) || !read(destination,&b,4U) ||
           a==b)continue;
        std::array<uint32_t,128> ids{};
        if(!read(indices,ids.data(),ncorners*4U))continue;
        std::vector<std::array<float,3>> polygon;
        polygon.reserve(ncorners);
        bool good=true;
        for(uint32_t j=0;j<ncorners;++j) {
            if(ids[j]>=nvertices) {good=false;break;}
            std::array<float,3> p{};
            if(!read(uintptr_t(vertices)+uintptr_t(ids[j])*12U,
                     p.data(),sizeof(float)*3U) ||
               !std::isfinite(p[0]) || !std::isfinite(p[1]) ||
               !std::isfinite(p[2])) {
                good=false;break;
            }
            polygon.push_back(p);
        }
        if(good && grid.add_portal(a,b,polygon))++added;
    }
    return added;
}


} // namespace jkgm
