#pragma once
// Independent, read-only reflected-camera portal traversal. Do NOT reuse
// JK's main-camera portal list or change native rdCamera/SithSector state.
// The reflected eye can be behind a mirror (or below water), so traversal
// starts at the sector(s) owning visible reflective receivers, not at the
// physical sector containing the mirrored eye. Unknown topology fails open.
#include "world_cache_portals.hpp"
#include "planar_reflection_camera.hpp"
#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <limits>
#include <unordered_set>
#include <vector>

namespace jkgm {
struct planar_reflection_portal_stats {
    bool valid=false,used_portals=false;
    uint32_t sectors_total=0,seed_sectors=0,visible_sectors=0;
    uint32_t portals_considered=0,portals_reached=0;
    uint32_t surfaces_reachable=0;
    char const *reason="unavailable";
};

// A screen-space rectangle deliberately OVERestimates the projected polygon.
// Unioning multiple portal paths per sector can only admit extra geometry.
struct planar_reflection_portal_rect {
    float left=-1.0f,right=1.0f,bottom=-1.0f,top=1.0f;
    bool empty=false;
};
inline planar_reflection_portal_rect planar_reflection_portal_project_aabb(
    std::array<float,3> const &lo,std::array<float,3> const &hi,
    gpu_3do_matrix34 const &view,float tan_half_fov,float aspect)
{
    planar_reflection_portal_rect full{};
    if(!gpu_3do_matrix_finite(view) ||
       !std::isfinite(tan_half_fov) || tan_half_fov<=0.0f ||
       !std::isfinite(aspect) || aspect<=0.0f)return full;
    for(int i=0;i<3;++i)
        if(!std::isfinite(lo[i]) || !std::isfinite(hi[i]) ||
           hi[i]<lo[i])return full;
    float const h=tan_half_fov,v=h/aspect;
    float min_x=std::numeric_limits<float>::infinity();
    float max_x=-min_x,min_y=min_x,max_y=-min_x;
    for(int n=0;n<8;++n) {
        std::array<float,3> p{{
            (n&1)?hi[0]:lo[0],(n&2)?hi[1]:lo[1],
            (n&4)?hi[2]:lo[2]}};
        auto const q=gpu_3do_native_transform_point(view,p);
        // Near-plane intersection requires actual polygon clipping.
        // Expand to the FULL screen instead; never discard its neighbor.
        if(!std::isfinite(q[0]) || !std::isfinite(q[1]) ||
           !std::isfinite(q[2]) || q[1]<=0.1f)return full;
        float const x=q[0]/(h*q[1]),y=q[2]/(v*q[1]);
        if(!std::isfinite(x) || !std::isfinite(y))return full;
        min_x=std::min(min_x,x);max_x=std::max(max_x,x);
        min_y=std::min(min_y,y);max_y=std::max(max_y,y);
    }
    // One-pixel-ish guard band also tolerates minor mirror-basis errors.
    constexpr float pad=0.02f;
    planar_reflection_portal_rect out{};
    out.empty=max_x< -1.0f-pad || min_x>1.0f+pad ||
              max_y< -1.0f-pad || min_y>1.0f+pad;
    if(out.empty)return out;
    out.left=std::max(-1.0f,min_x-pad);
    out.right=std::min(1.0f,max_x+pad);
    out.bottom=std::max(-1.0f,min_y-pad);
    out.top=std::min(1.0f,max_y+pad);
    return out;
}
inline planar_reflection_portal_rect planar_reflection_portal_intersect(
    planar_reflection_portal_rect const &a,
    planar_reflection_portal_rect const &b)
{
    planar_reflection_portal_rect out{};
    out.left=std::max(a.left,b.left);
    out.right=std::min(a.right,b.right);
    out.bottom=std::max(a.bottom,b.bottom);
    out.top=std::min(a.top,b.top);
    out.empty=a.empty || b.empty || out.left>out.right ||
              out.bottom>out.top;
    return out;
}

class planar_reflection_portal_graph {
    struct edge {
        uint32_t destination=0,surface_index=0;
        std::array<float,3> low{},high{};
    };
    struct sector {
        world_cache_sector_span span{};
        std::vector<edge> portals;
    };
    std::vector<sector> sectors_;
    std::vector<int32_t> surface_owner_;
    uintptr_t world_=0;
    uint64_t revision_=0;
    bool built_=false,valid_=false;

    static bool pointer_ok(uint64_t a,uint64_t bytes) {
        return a>=0x10000ULL && a+bytes<=0x7ff00000ULL;
    }
    static bool sector_index(uint64_t ptr,uint64_t table,
                             uint32_t count,uint32_t &out)
    {
        if(ptr<table || ptr>=table+uint64_t(count)*0x98U ||
           ((ptr-table)%0x98U)!=0U)return false;
        out=uint32_t((ptr-table)/0x98U);
        return true;
    }

public:
    void clear() {
        sectors_.clear();surface_owner_.clear();
        world_=0;revision_=0;built_=false;valid_=false;
    }
    bool ready()const{return built_ && valid_;}

    // Rebuild only when world topology changes. Live adjoin flags are
    // deliberately not treated as static topology; the traversal follows
    // all valid geometric links to remain conservative under COG changes.
    bool ensure(decal_native_read const &read,uintptr_t world,
                uint64_t revision,decal_world_geometry const &geometry,
                world_cache_mesh const &mesh,
                std::vector<uint32_t> const &lookup)
    {
        if(built_ && world_==world && revision_==revision)return valid_;
        clear();
        built_=true;world_=world;revision_=revision;
        if(!read || !pointer_ok(world,0xc0U) ||
           !geometry.valid_world || geometry.world!=world ||
           geometry.surface_count==0U ||
           lookup.size()!=geometry.surface_count)return false;
        auto read32=[&](uintptr_t address,uint32_t &value) {
            return bool(read(address,&value,4U));
        };
        uint32_t count=0,table=0;
        if(!read32(world+0x4cU,count) ||
           !read32(world+0x50U,table) ||
           !count || count>65536U ||
           !pointer_ok(table,uint64_t(count)*0x98U))return false;
        sectors_.resize(count);
        surface_owner_.assign(geometry.surface_count,-1);
        std::vector<uint32_t> heads(count,0U);
        for(uint32_t i=0;i<count;++i) {
            uint32_t metadata[3]{};
            if(!read(uintptr_t(table)+uint64_t(i)*0x98U+0x24U,
                     metadata,sizeof(metadata)))return false;
            uint32_t const n=metadata[0],surface_ptr=metadata[1];
            heads[i]=metadata[2]; // SithSector.adjoins +0x2c
            if(!n)continue;
            uint64_t const start=geometry.surfaces_addr;
            uint64_t const ptr=surface_ptr;
            if(!pointer_ok(ptr,uint64_t(n)*0x5cU) ||
               ptr<start || (ptr-start)%0x5cU ||
               uint64_t(n)>geometry.surface_count-
                    std::min<uint64_t>((ptr-start)/0x5cU,
                                       geometry.surface_count))
                return false;
            uint64_t const first=(ptr-start)/0x5cU;
            if(first>=geometry.surface_count ||
               first+uint64_t(n)>geometry.surface_count)return false;
            sectors_[i].span={uint32_t(first),n};
            for(uint32_t j=0;j<n;++j) {
                auto &owner=surface_owner_[size_t(first)+j];
                if(owner>=0)return false;
                owner=int32_t(i);
            }
        }
        // Linked adjoins can have no MAT and so can be absent from the
        // render cache. In that case read only their static native polygon
        // to obtain a conservative world-space AABB for screen clipping.
        std::unordered_set<uintptr_t> visited;
        visited.reserve(count*2U);
        for(uint32_t i=0;i<count;++i) {
            uint32_t next=heads[i];
            for(unsigned step=0;next!=0U;++step) {
                if(step>65536U || visited.size()>262144U ||
                   !pointer_ok(next,0x14U) ||
                   !visited.insert(next).second)return false;
                uint32_t link[5]{};
                if(!read(next,link,sizeof(link)))return false;
                uint32_t dest=0,surface_index=0;
                if(!sector_index(link[1],table,count,dest) ||
                   !pointer_ok(link[2],0x5cU) ||
                   uint64_t(link[2])<geometry.surfaces_addr ||
                   (uint64_t(link[2])-geometry.surfaces_addr)%0x5cU)
                    return false;
                uint64_t const si=(uint64_t(link[2])-
                                   geometry.surfaces_addr)/0x5cU;
                if(si>=surface_owner_.size() ||
                   surface_owner_[size_t(si)]!=int32_t(i))return false;
                surface_index=uint32_t(si);
                edge e{};e.destination=dest;e.surface_index=surface_index;
                auto const cached=lookup[si];
                if(cached<mesh.surfaces.size() &&
                   mesh.surfaces[cached].surface_index==si) {
                    e.low=mesh.surfaces[cached].bounds_min;
                    e.high=mesh.surfaces[cached].bounds_max;
                } else {
                    uint32_t nv=0,indices=0;
                    if(!read32(uintptr_t(link[2])+0x28U,nv) ||
                       !read32(uintptr_t(link[2])+0x2cU,indices) ||
                       nv<3U || nv>128U ||
                       !pointer_ok(indices,uint64_t(nv)*4U))return false;
                    e.low.fill(std::numeric_limits<float>::infinity());
                    e.high.fill(-std::numeric_limits<float>::infinity());
                    for(uint32_t j=0;j<nv;++j) {
                        uint32_t vi=0;std::array<float,3> pos{};
                        if(!read32(uintptr_t(indices)+j*4U,vi) ||
                           vi>=geometry.vertex_count ||
                           !read(geometry.vertices_addr+uintptr_t(vi)*12U,
                                 pos.data(),sizeof(pos)))return false;
                        for(int a=0;a<3;++a) {
                            if(!std::isfinite(pos[a]))return false;
                            e.low[a]=std::min(e.low[a],pos[a]);
                            e.high[a]=std::max(e.high[a],pos[a]);
                        }
                    }
                }
                for(int a=0;a<3;++a)
                    if(!std::isfinite(e.low[a]) ||
                       !std::isfinite(e.high[a]) ||
                       e.high[a]<e.low[a])return false;
                sectors_[i].portals.push_back(e);
                next=link[4]; // SithSurfaceAdjoin.next +0x10
            }
        }
        valid_=true;
        return true;
    }

    // Receivers are MAIN-camera staged real reflective surfaces on the
    // selected plane. Their own sectors are seed regions; no assumption is
    // made about the virtual eye's position inside SithWorld.
    planar_reflection_portal_stats traverse(
        std::vector<uint32_t> const &receiver_surfaces,
        world_cache_mesh const &mesh,
        std::vector<uint32_t> const &lookup,
        gpu_3do_matrix34 const &reflection_view,float tan_half_fov,
        float aspect,std::vector<world_cache_sector_span> &out)const
    {
        out.clear();
        planar_reflection_portal_stats stats{};
        stats.sectors_total=uint32_t(sectors_.size());
        if(!ready() || receiver_surfaces.empty() ||
           lookup.size()!=surface_owner_.size()) {
            stats.reason="topology_or_receivers_unavailable";return stats;
        }
        std::vector<planar_reflection_portal_rect> windows(sectors_.size());
        std::vector<uint8_t> reachable(sectors_.size(),0U);
        std::vector<uint32_t> pending;
        for(uint32_t const si:receiver_surfaces) {
            if(si>=lookup.size() || surface_owner_[si]<0)continue;
            uint32_t const cached=lookup[si];
            if(cached>=mesh.surfaces.size() ||
               mesh.surfaces[cached].surface_index!=si)continue;
            auto const &surface=mesh.surfaces[cached];
            auto rect=planar_reflection_portal_project_aabb(
                surface.bounds_min,surface.bounds_max,reflection_view,
                tan_half_fov,aspect);
            if(rect.empty)continue;
            uint32_t const owner=uint32_t(surface_owner_[si]);
            auto seed=[&](uint32_t sector_id) {
                if(!reachable[sector_id]) {
                    reachable[sector_id]=1U;windows[sector_id]=rect;
                    pending.push_back(sector_id);++stats.seed_sectors;
                } else {
                    auto &w=windows[sector_id];
                    w.left=std::min(w.left,rect.left);
                    w.right=std::max(w.right,rect.right);
                    w.bottom=std::min(w.bottom,rect.bottom);
                    w.top=std::max(w.top,rect.top);
                }
            };
            seed(owner);
            // A passable water/mirror adjoin may be owned by the sector
            // opposite the physical reflecting half-space. Seed BOTH
            // adjoining sectors where the receiver itself is a portal:
            // the mirror's virtual eye cannot identify a unique side.
            for(auto const &portal:sectors_[owner].portals)
                if(portal.surface_index==si)seed(portal.destination);
        }
        if(pending.empty()) {
            stats.reason="no_visible_receiver_seeds";return stats;
        }
        // Worklist union converges conservatively. A safety cap never
        // returns an incomplete set: the caller falls back to full scan.
        size_t edges=0;
        for(auto const &s:sectors_)edges+=s.portals.size();
        size_t const max_iterations=4U*(edges+sectors_.size()+1U);
        size_t visits=0;
        for(size_t q=0;q<pending.size();++q) {
            if(++visits>max_iterations) {
                stats.reason="portal_iteration_limit";out.clear();
                return stats;
            }
            uint32_t const owner=pending[q];
            auto const window=windows[owner];
            for(auto const &portal:sectors_[owner].portals) {
                ++stats.portals_considered;
                auto const rect=planar_reflection_portal_project_aabb(
                    portal.low,portal.high,reflection_view,
                    tan_half_fov,aspect);
                auto const clipped=planar_reflection_portal_intersect(
                    window,rect);
                if(clipped.empty)continue;
                ++stats.portals_reached;
                uint32_t const dest=portal.destination;
                if(!reachable[dest]) {
                    reachable[dest]=1U;windows[dest]=clipped;
                    pending.push_back(dest);
                    continue;
                }
                auto &known=windows[dest];
                constexpr float epsilon=0.0001f;
                if(clipped.left>=known.left-epsilon &&
                   clipped.right<=known.right+epsilon &&
                   clipped.bottom>=known.bottom-epsilon &&
                   clipped.top<=known.top+epsilon)continue;
                known.left=std::min(known.left,clipped.left);
                known.right=std::max(known.right,clipped.right);
                known.bottom=std::min(known.bottom,clipped.bottom);
                known.top=std::max(known.top,clipped.top);
                pending.push_back(dest);
            }
        }
        out.reserve(sectors_.size());
        for(size_t i=0;i<sectors_.size();++i)if(reachable[i]) {
            ++stats.visible_sectors;
            auto const span=sectors_[i].span;
            if(span.surface_count) {
                stats.surfaces_reachable+=span.surface_count;
                out.push_back(span);
            }
        }
        stats.valid=stats.visible_sectors>0U;
        stats.used_portals=stats.valid;
        stats.reason=stats.valid?"ok":"no_reachable_sector";
        return stats;
    }
};
} // namespace jkgm
