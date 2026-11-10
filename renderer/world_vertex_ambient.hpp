#pragma once
// CPU-only authored world-vertex ambient probes with local, passable-adjoin
// interpolation. Sector ExtraLight is a live world-vertex brightness floor;
// the camera-derived Thing ambient is not part of the probe.
// No GL resources or per-frame scan of the SithWorld surface table.
#include "world_cache.hpp"
#include "light_intensity_range.hpp"
#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <limits>
#include <initializer_list>
#include <vector>
#include <unordered_map>

namespace jkgm {

struct world_vertex_ambient_stats {
    uint32_t surfaces_seen=0, surfaces_sampled=0, surfaces_skipped=0;
    uint32_t vertices_sampled=0,portals_captured=0;
};

enum class world_vertex_ambient_result { none,local,mixed,portal_only };

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
        // Immutable authored brightness (vertex + face ExtraLight) before
        // applying the owning sector's runtime ExtraLight minimum.
        std::vector<float> base_lights;
    };
    struct sector_light_state {
        uintptr_t native_sector=0;
        float floor=0.0f;
        std::vector<key> cells; // only cells belonging to this SithSector
    };
    std::unordered_map<key,cell,key_hash> cells_;
    std::unordered_map<uint32_t,sector_light_state> sector_lights_;
    using point=std::array<float,3>;
    using dpoint=std::array<double,3>;
    struct portal_triangle { std::array<point,3> v{}; };
    struct portal {
        uint32_t a=0,b=0;
        point lo{},hi{};
        std::vector<portal_triangle> triangles;
    };
    std::vector<portal> portals_;
    std::unordered_map<uint32_t,std::vector<size_t>> portal_sectors_;

    static dpoint sub(dpoint const &a,dpoint const &b) {
        return {{a[0]-b[0],a[1]-b[1],a[2]-b[2]}};
    }
    static double dot(dpoint const &a,dpoint const &b) {
        return a[0]*b[0]+a[1]*b[1]+a[2]*b[2];
    }
    static dpoint to_double(point const &p) {
        return {{p[0],p[1],p[2]}};
    }
    static double segment_distance2(dpoint const &p,
                                    dpoint const &a,dpoint const &b) {
        auto const ab=sub(b,a),ap=sub(p,a);
        double const len2=dot(ab,ab);
        double const t=len2>1.0e-20
            ?std::max(0.0,std::min(1.0,dot(ap,ab)/len2)):0.0;
        dpoint q{{a[0]+ab[0]*t,a[1]+ab[1]*t,a[2]+ab[2]*t}};
        auto const delta=sub(p,q);
        return dot(delta,delta);
    }
    static double triangle_distance2(point const &position,
                                     portal_triangle const &tri) {
        auto const p=to_double(position),a=to_double(tri.v[0]);
        auto const b=to_double(tri.v[1]),c=to_double(tri.v[2]);
        auto const ab=sub(b,a),ac=sub(c,a),ap=sub(p,a);
        double const aa=dot(ab,ab),abac=dot(ab,ac),cc=dot(ac,ac);
        double const denom=aa*cc-abac*abac;
        if(denom>1.0e-20) {
            double const u=dot(ap,ab),v=dot(ap,ac);
            double const beta=(u*cc-v*abac)/denom;
            double const gamma=(v*aa-u*abac)/denom;
            if(beta>=0.0 && gamma>=0.0 && beta+gamma<=1.0) {
                dpoint q{{a[0]+beta*ab[0]+gamma*ac[0],
                          a[1]+beta*ab[1]+gamma*ac[1],
                          a[2]+beta*ab[2]+gamma*ac[2]}};
                auto const delta=sub(p,q);
                return dot(delta,delta);
            }
        }
        return std::min(segment_distance2(p,a,b),
               std::min(segment_distance2(p,b,c),
                        segment_distance2(p,c,a)));
    }
    double portal_distance2(portal const &entry,point const &position) const {
        double const r=radius_;
        for(int i=0;i<3;++i)
            if(double(position[i])<double(entry.lo[i])-r ||
               double(position[i])>double(entry.hi[i])+r)
                return std::numeric_limits<double>::infinity();
        double result=std::numeric_limits<double>::infinity();
        for(auto const &triangle:entry.triangles)
            result=std::min(result,triangle_distance2(position,triangle));
        return result;
    }

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
        sector_lights_.clear();
        portals_.clear();
        portal_sectors_.clear();
        samples_=0;
        radius_=std::isfinite(radius) && radius>=0.25f &&
                radius<=4.0f ? radius:1.0f;
    }
    size_t cell_count() const {return cells_.size();}
    size_t tracked_sector_count() const {return sector_lights_.size();}
    size_t portal_count() const {return portals_.size();}
    uint32_t sample_count() const {return samples_;}
    float radius() const {return radius_;}

    // Register BEFORE inserting a sector's first sample. Unlike the Thing
    // camera ambient, SithSector +0x08 is the live world-lighting floor.
    // Duplicate surfaces in one sector register the same validated pointer.
    bool register_sector(uint32_t id,uintptr_t native_sector,
                         float extra,bool overbright) {
        if(!decal_native_address_ok(native_sector) ||
           !std::isfinite(extra))return false;
        float const floor=std::max(0.0f,
            clamp_authored_extra_light(extra,overbright));
        auto const it=sector_lights_.find(id);
        if(it!=sector_lights_.end())
            return it->second.native_sector==native_sector;
        sector_light_state state{};
        state.native_sector=native_sector;
        state.floor=floor;
        sector_lights_.emplace(id,std::move(state));
        return true;
    }

    bool add(uint32_t sector,std::array<float,3> const &position,float light) {
        if(!std::isfinite(light) || light<0.0f ||
           samples_==std::numeric_limits<uint32_t>::max())return false;
        std::array<int32_t,3> c{};
        if(!cell_coord(position,c))return false;
        key const k{sector,c[0],c[1],c[2]};
        auto const registered=sector_lights_.find(sector);
        auto [it,inserted]=cells_.try_emplace(k);
        auto &cell=it->second;
        if(cell.count==std::numeric_limits<uint32_t>::max())return false;
        if(inserted && registered!=sector_lights_.end())
            registered->second.cells.push_back(k);
        for(size_t i=0;i<3;++i)cell.position_sum[i]+=position[i];
        cell.base_lights.push_back(light);
        float const floor=registered!=sector_lights_.end()?
                          registered->second.floor:0.0f;
        cell.light_sum+=std::max(light,floor);
        ++cell.count;
        ++samples_;
        return true;
    }

    struct refresh_stats {
        uint32_t checked=0,changed=0,cells=0,vertices=0,unreadable=0;
    };

    // This detects *every tick* of a SetSectorLight transition, not merely
    // the initial verb invocation. No world geometry scan or full grid
    // rebuild. All updates are confined to the changed sector's cells.
    template<class Read>
    refresh_stats refresh_sector_floors(Read const &read,bool overbright) {
        refresh_stats stats{};
        for(auto &entry:sector_lights_) {
            auto &sector=entry.second;
            ++stats.checked;
            float extra=0.0f;
            if(!read(sector.native_sector+0x08U,&extra,sizeof(extra)) ||
               !std::isfinite(extra)) {
                ++stats.unreadable;
                continue; // Fail closed: retain last valid floor.
            }
            float const floor=std::max(0.0f,
                clamp_authored_extra_light(extra,overbright));
            if(floor==sector.floor)continue;
            sector.floor=floor;
            ++stats.changed;
            for(auto const &k:sector.cells) {
                auto found=cells_.find(k);
                if(found==cells_.end())continue;
                auto &cell=found->second;
                double sum=0.0;
                for(float authored:cell.base_lights)
                    sum+=std::max(authored,floor);
                cell.light_sum=sum;
                ++stats.cells;
                stats.vertices+=cell.count;
            }
        }
        return stats;
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

    // The portal must be explicitly passable in the native Adjoin record.
    // Kept in both sector indices so a Thing crossing its plane gets the
    // identical mixed value from either owner at the same world position.
    bool add_portal(uint32_t a,uint32_t b,
                    std::vector<point> const &polygon) {
        if(a==b || polygon.size()<3U || polygon.size()>128U ||
           portals_.size()>=8192U)return false;
        portal entry{};
        entry.a=a;entry.b=b;
        entry.lo={{std::numeric_limits<float>::max(),
                   std::numeric_limits<float>::max(),
                   std::numeric_limits<float>::max()}};
        entry.hi={{-std::numeric_limits<float>::max(),
                   -std::numeric_limits<float>::max(),
                   -std::numeric_limits<float>::max()}};
        for(auto const &p:polygon) {
            for(size_t i=0;i<3;++i) {
                if(!std::isfinite(p[i]))return false;
                entry.lo[i]=std::min(entry.lo[i],p[i]);
                entry.hi[i]=std::max(entry.hi[i],p[i]);
            }
        }
        // Convex SithSurface polygon: the fan is sufficient to compute
        // exact distance to the finite opening, not its infinite plane.
        for(size_t i=1;i+1<polygon.size();++i) {
            portal_triangle tri{{{polygon[0],polygon[i],polygon[i+1]}}};
            auto const a0=to_double(tri.v[0]);
            auto const ab=sub(to_double(tri.v[1]),a0);
            auto const ac=sub(to_double(tri.v[2]),a0);
            double const xx=ab[1]*ac[2]-ab[2]*ac[1];
            double const yy=ab[2]*ac[0]-ab[0]*ac[2];
            double const zz=ab[0]*ac[1]-ab[1]*ac[0];
            if(xx*xx+yy*yy+zz*zz>1.0e-18)
                entry.triangles.push_back(tri);
        }
        if(entry.triangles.empty())return false;
        size_t const index=portals_.size();
        portals_.push_back(std::move(entry));
        portal_sectors_[a].push_back(index);
        portal_sectors_[b].push_back(index);
        return true;
    }

    world_vertex_ambient_result sample_with_portals(
        uint32_t sector,point const &position,float &ambient) const
    {
        float own=0.0f;
        bool const has_own=sample(sector,position,own);
        float neighbor=0.0f;
        bool has_neighbor=false;
        double closest=double(radius_)*radius_;
        auto const found=portal_sectors_.find(sector);
        if(found!=portal_sectors_.end())for(size_t const index:found->second) {
            auto const &entry=portals_[index];
            double const distance2=portal_distance2(entry,position);
            if(distance2>closest)continue;
            uint32_t const other=entry.a==sector?entry.b:entry.a;
            float candidate=0.0f;
            if(!sample(other,position,candidate))continue;
            neighbor=candidate;
            has_neighbor=true;
            closest=distance2;
        }
        if(has_own && has_neighbor) {
            double const normalized=std::min(1.0,
                std::sqrt(closest)/double(radius_));
            double const smooth=normalized*normalized*
                                (3.0-2.0*normalized);
            // At the portal plane, both owner choices yield the same
            // half-and-half result. Away from it, stay in the home sector.
            double const other_weight=0.5*(1.0-smooth);
            ambient=static_cast<float>(
                double(own)*(1.0-other_weight)+
                double(neighbor)*other_weight);
            return world_vertex_ambient_result::mixed;
        }
        if(has_own) {
            ambient=own;
            return world_vertex_ambient_result::local;
        }
        if(has_neighbor) {
            // Sparse sector: use adjacent samples only through the opening,
            // not native JKL ambient. Avoids a seam when local cells vanish.
            ambient=neighbor;
            return world_vertex_ambient_result::portal_only;
        }
        return world_vertex_ambient_result::none;
    }

};

// Capture directly from native SithWorld rather than world_cache_mesh:
// invisible, non-MAT passable adjoins are omitted by the render mesh and
// must still link the probe fields. Capture once, never per Thing/frame.
template<class Read>
uint32_t capture_world_vertex_ambient_portals(
    Read const &read,uintptr_t world,world_vertex_ambient_grid &grid)
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
           !std::isfinite(sector_extra) || !std::isfinite(face_extra) ||
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
        // Match the cached world's Gouraud path: the sector's authored
        // ExtraLight is a floor, while face ExtraLight is added to the
        // static vertex sample. This is NOT Thing/camera ambient.
        if(!grid.register_sector(sector_id,sector,sector_extra,overbright)) {
            ++stats.surfaces_skipped;continue;
        }
        float const extra=clamp_authored_extra_light(face_extra,overbright);
        uint32_t accepted=0;
        for(uint32_t i=0;i<n;++i) {
            float const authored=clamp_authored_light(intensity[i],overbright);
            float const base_light=std::max(0.0f,authored+extra);
            if(grid.add(sector_id,mesh.vertices[base+i].position,base_light))
                ++accepted;
        }
        if(accepted) {
            ++stats.surfaces_sampled;
            stats.vertices_sampled+=accepted;
        } else ++stats.surfaces_skipped;
    }
    stats.portals_captured=capture_world_vertex_ambient_portals(
        read,world,grid);
    return true;
}
} // namespace jkgm
