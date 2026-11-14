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
#include <functional>
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


/*
 * OpenJKDF2 sithRender_BuildVisibleSectorList uses rdPrimit3_ClipFace on
 * the *actual portal polygon*, then projects the clipped vertices and
 * constructs a smaller screen rectangle for the adjoining sector.  The old
 * reflected path projected the portal AABB instead (and returned the full
 * screen whenever one corner touched the near plane).  This CPU-only
 * Sutherland-Hodgman equivalent keeps a separate virtual-camera state and
 * avoids touching JK's rdCamera, render ticks, or COG messages.
 */
inline planar_reflection_portal_rect planar_reflection_portal_clip_polygon(
    std::vector<std::array<float,3>> const &world_vertices,
    gpu_3do_matrix34 const &view,float tan_half_fov,float aspect,
    planar_reflection_portal_rect const &parent,bool &valid,
    float near_plane=0.05f)
{
    valid=false;
    planar_reflection_portal_rect empty{};empty.empty=true;
    if(world_vertices.size()<3U || world_vertices.size()>128U ||
       !gpu_3do_matrix_finite(view) ||
       !std::isfinite(tan_half_fov) || tan_half_fov<=0.0f ||
       !std::isfinite(aspect) || aspect<=0.0f ||
       !std::isfinite(near_plane) || near_plane<=0.0f ||
       parent.empty || parent.left>parent.right ||
       parent.bottom>parent.top)return empty;
    float const h=tan_half_fov,v=h/aspect;
    std::vector<std::array<float,3>> polygon;
    polygon.reserve(world_vertices.size()+12U);
    for(auto const &p:world_vertices) {
        for(float x:p)if(!std::isfinite(x))return empty;
        auto const q=gpu_3do_native_transform_point(view,p);
        for(float x:q)if(!std::isfinite(x))return empty;
        polygon.push_back(q);
    }
    // Camera coordinates: X right, Y forward, Z up (Sith rdCamera).
    // Clip first to near, then to the inherited four perspective planes.
    for(int plane=0;plane<5;++plane) {
        if(polygon.empty())break;
        auto distance=[&](std::array<float,3> const &p)->double {
            switch(plane) {
            case 0:return double(p[1])-near_plane;
            case 1:return double(p[0])-double(parent.left)*h*p[1];
            case 2:return double(parent.right)*h*p[1]-p[0];
            case 3:return double(p[2])-double(parent.bottom)*v*p[1];
            default:return double(parent.top)*v*p[1]-p[2];
            }
        };
        std::vector<std::array<float,3>> clipped;
        clipped.reserve(polygon.size()+2U);
        auto previous=polygon.back();
        double previous_distance=distance(previous);
        bool previous_inside=previous_distance>= -1e-6;
        for(auto const &current:polygon) {
            double const current_distance=distance(current);
            bool const current_inside=current_distance>= -1e-6;
            if(previous_inside!=current_inside) {
                double const denom=previous_distance-current_distance;
                if(std::fabs(denom)<1e-20)return empty;
                double const t=std::max(0.0,std::min(1.0,
                    previous_distance/denom));
                std::array<float,3> cut{};
                for(int a=0;a<3;++a)
                    cut[a]=float(double(previous[a])+t*
                                  (double(current[a])-previous[a]));
                clipped.push_back(cut);
            }
            if(current_inside)clipped.push_back(current);
            previous=current;
            previous_distance=current_distance;
            previous_inside=current_inside;
        }
        polygon.swap(clipped);
        if(polygon.size()>256U)return empty;
    }
    valid=true;
    if(polygon.size()<3U)return empty;
    float lo_x=std::numeric_limits<float>::infinity();
    float hi_x=-lo_x,lo_y=lo_x,hi_y=-lo_x;
    for(auto const &p:polygon) {
        if(p[1]<=0.0f) {valid=false;return empty;}
        float const x=p[0]/(h*p[1]), y=p[2]/(v*p[1]);
        if(!std::isfinite(x) || !std::isfinite(y)) {
            valid=false;return empty;
        }
        lo_x=std::min(lo_x,x);hi_x=std::max(hi_x,x);
        lo_y=std::min(lo_y,y);hi_y=std::max(hi_y,y);
    }
    // The source uses a small pixel guard band for raster precision.
    // Work with normalized viewport bounds; 0.01 is intentionally generous.
    constexpr float pad=0.01f;
    planar_reflection_portal_rect out{};
    out.left=std::max(parent.left,lo_x-pad);
    out.right=std::min(parent.right,hi_x+pad);
    out.bottom=std::max(parent.bottom,lo_y-pad);
    out.top=std::min(parent.top,hi_y+pad);
    out.empty=out.left>out.right || out.bottom>out.top;
    return out;
}

class planar_reflection_portal_graph {
    struct edge {
        uint32_t destination=0,surface_index=0;
        uintptr_t native_adjoin=0;
        std::array<float,3> low{},high{},normal{};
        std::vector<std::array<float,3>> polygon;
    };
    struct sector {
        world_cache_sector_span span{};
        std::vector<edge> portals;
    };
    std::vector<sector> sectors_;
    std::vector<int32_t> surface_owner_;
    uintptr_t sector_table_=0;
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
        world_=0;revision_=0;sector_table_=0;built_=false;valid_=false;
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
        sector_table_=table;
        // World geometry retains the authored n-gon, unlike the triangulated
        // GPU cache. It also covers opaque portals without a GPU MAT.
        std::vector<decal_world_surface_geometry const*> authored(
            geometry.surface_count,nullptr);
        for(auto const &surface:geometry.receivers)
            if(surface.surface_index<authored.size())
                authored[surface.surface_index]=&surface;
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
                edge e{};
                e.destination=dest;e.surface_index=surface_index;
                e.native_adjoin=next;
                auto const *original=authored[si];
                if(original && original->polygon.size()>=3U &&
                   original->polygon.size()<=128U) {
                    e.polygon=original->polygon;
                    e.normal=original->normal;
                } else {
                    // A material-less portal can be missing from the GPU
                    // mesh. Read its native face indices and world vertices.
                    uint32_t nv=0,indices=0;
                    if(!read32(uintptr_t(link[2])+0x28U,nv) ||
                       !read32(uintptr_t(link[2])+0x2cU,indices) ||
                       nv<3U || nv>128U ||
                       !pointer_ok(indices,uint64_t(nv)*4U) ||
                       !read(uintptr_t(link[2])+0x44U,
                             e.normal.data(),sizeof(e.normal)))return false;
                    e.polygon.reserve(nv);
                    for(uint32_t j=0;j<nv;++j) {
                        uint32_t vi=0;std::array<float,3> pos{};
                        if(!read32(uintptr_t(indices)+j*4U,vi) ||
                           vi>=geometry.vertex_count ||
                           !read(geometry.vertices_addr+uintptr_t(vi)*12U,
                                 pos.data(),sizeof(pos)))return false;
                        e.polygon.push_back(pos);
                    }
                }
                e.low.fill(std::numeric_limits<float>::infinity());
                e.high.fill(-std::numeric_limits<float>::infinity());
                for(float n:e.normal)if(!std::isfinite(n))return false;
                for(auto const &p:e.polygon)for(int a=0;a<3;++a) {
                    if(!std::isfinite(p[a]))return false;
                    e.low[a]=std::min(e.low[a],p[a]);
                    e.high[a]=std::max(e.high[a],p[a]);
                }
                sectors_[i].portals.push_back(e);
                next=link[4]; // SithSurfaceAdjoin.next +0x10
            }
        }
        valid_=true;
        return true;
    }

    // These spans are consumed by the same GPU cache used for the normal
    // camera. A failed native read or numerical instability fails OPEN to the
    // existing whole-world reflected-frustum fallback.
    planar_reflection_portal_stats traverse(
        decal_native_read const &read,
        std::vector<uint32_t> const &receiver_surfaces,
        world_cache_mesh const &mesh,
        std::vector<uint32_t> const &lookup,
        gpu_3do_matrix34 const &reflection_view,
        std::array<float,3> const &reflection_eye,
        float tan_half_fov,float aspect,
        std::vector<world_cache_sector_span> &out,
        std::vector<planar_reflection_portal_rect> *sector_windows=nullptr,
        std::vector<uint8_t> *thing_sector_mask=nullptr)const
    {
        out.clear();
        if(sector_windows)sector_windows->clear();
        if(thing_sector_mask)thing_sector_mask->clear();
        planar_reflection_portal_stats stats{};
        stats.sectors_total=uint32_t(sectors_.size());
        if(!ready() || !read || receiver_surfaces.empty() ||
           lookup.size()!=surface_owner_.size() ||
           !gpu_3do_matrix_finite(reflection_view) ||
           !std::isfinite(tan_half_fov) || tan_half_fov<=0.0f ||
           !std::isfinite(aspect) || aspect<=0.0f) {
            stats.reason="topology_or_receivers_unavailable";return stats;
        }
        for(float x:reflection_eye)if(!std::isfinite(x)) {
            stats.reason="invalid_reflected_eye";return stats;
        }
        std::vector<planar_reflection_portal_rect> windows(sectors_.size());
        std::vector<uint8_t> reached(sectors_.size(),0U);
        std::vector<uint8_t> path(sectors_.size(),0U);
        struct seed {uint32_t sector=0;planar_reflection_portal_rect window{};};
        std::vector<seed> seeds;
        std::vector<uint8_t> seeded(sectors_.size(),0U);
        planar_reflection_portal_rect const full{};
        for(uint32_t const si:receiver_surfaces) {
            if(si>=lookup.size() || surface_owner_[si]<0)continue;
            uint32_t const cached=lookup[si];
            if(cached>=mesh.surfaces.size() ||
               mesh.surfaces[cached].surface_index!=si)continue;
            auto const &surface=mesh.surfaces[cached];
            // Conservative footprint of the visible reflective receiver.
            auto rect=planar_reflection_portal_project_aabb(
                surface.bounds_min,surface.bounds_max,reflection_view,
                tan_half_fov,aspect);
            if(rect.empty)continue;
            uint32_t const owner=uint32_t(surface_owner_[si]);
            auto seed_sector=[&](uint32_t id) {
                if(!seeded[id]) {
                    seeded[id]=1U;seeds.push_back({id,rect});
                    ++stats.seed_sectors;
                } else for(auto &existing:seeds)if(existing.sector==id) {
                    existing.window.left=std::min(existing.window.left,rect.left);
                    existing.window.right=std::max(existing.window.right,rect.right);
                    existing.window.bottom=std::min(existing.window.bottom,rect.bottom);
                    existing.window.top=std::max(existing.window.top,rect.top);
                    break;
                }
            };
            seed_sector(owner);
            // A mirror can place the virtual camera across a water adjoin.
            // Keep both sides only for such an actual receiver adjoin.
            for(auto const &portal:sectors_[owner].portals)
                if(portal.surface_index==si) {
                    uint32_t flags=0;
                    if(!read(portal.native_adjoin,&flags,4U)) {
                        stats.reason="unreadable_receiver_adjoin";
                        return stats;
                    }
                    if(flags&1U)seed_sector(portal.destination);
                }
        }
        if(seeds.empty()) {
            stats.reason="no_visible_receiver_seeds";return stats;
        }
        bool failed=false;
        char const *error="ok";
        size_t call_count=0;
        size_t const call_limit=sectors_.size()*8U+1U;
        std::function<void(uint32_t,planar_reflection_portal_rect const&)> visit;
        visit=[&](uint32_t owner,planar_reflection_portal_rect const &window) {
            if(failed || path[owner])return;
            if(++call_count>call_limit) {
                failed=true;error="recursive_visit_limit";return;
            }
            if(reached[owner]) {
                // Native sithRender_BuildVisibleSectorList widens an already
                // rendered sector to the FULL camera frustum on a revisit.
                windows[owner]=full;
                return;
            }
            reached[owner]=1U;
            windows[owner]=window;
            path[owner]=1U;
            for(auto const &portal:sectors_[owner].portals) {
                ++stats.portals_considered;
                uint32_t live_flags=0;
                if(!read(portal.native_adjoin,&live_flags,4U)) {
                    failed=true;error="unreadable_live_adjoin";break;
                }
                if((live_flags&1U)==0U)continue;
                // Native JK rejects adjoins seen from their back side.
                auto const &p=portal.polygon.front();
                float const distance=
                    (reflection_eye[0]-p[0])*portal.normal[0]+
                    (reflection_eye[1]-p[1])*portal.normal[1]+
                    (reflection_eye[2]-p[2])*portal.normal[2];
                if(!std::isfinite(distance)) {
                    failed=true;error="nonfinite_portal_plane";break;
                }
                // An imprecise normal must never silently hide a sector.
                float normal2=0.0f;
                for(float n:portal.normal)normal2+=n*n;
                if(normal2>1e-8f && distance< -0.0001f)continue;
                bool clip_valid=false;
                auto const clipped=planar_reflection_portal_clip_polygon(
                    portal.polygon,reflection_view,tan_half_fov,aspect,
                    window,clip_valid);
                if(!clip_valid) {
                    failed=true;error="invalid_portal_clip";break;
                }
                if(clipped.empty)continue;
                ++stats.portals_reached;
                visit(portal.destination,clipped);
                if(failed)break;
            }
            path[owner]=0U;
        };
        for(auto const &entry:seeds) {
            visit(entry.sector,entry.window);
            if(failed)break;
        }
        if(failed) {
            out.clear();stats.reason=error;return stats;
        }
        out.reserve(sectors_.size());
        for(size_t i=0;i<sectors_.size();++i)if(reached[i]) {
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
        if(stats.valid && sector_windows)
            *sector_windows=std::move(windows);
        if(stats.valid && thing_sector_mask) {
            // SithRender_BuildVisibleSectorsThingList includes Things from
            // nearby adjoining sectors in addition to world-visible ones.
            // Conservatively extend one and two links from the native list,
            // without excluding an object whose owner pointer is invalid.
            *thing_sector_mask=reached;
            std::vector<uint8_t> frontier=reached;
            for(int depth=0;depth<2;++depth) {
                std::vector<uint8_t> next(frontier.size(),0U);
                for(size_t i=0;i<frontier.size();++i)if(frontier[i]) {
                    for(auto const &portal:sectors_[i].portals) {
                        uint32_t flags=0;
                        if(!read(portal.native_adjoin,&flags,4U)) {
                            thing_sector_mask->clear();
                            return stats; // fail open for models only
                        }
                        if((flags&1U) &&
                           !(*thing_sector_mask)[portal.destination]) {
                            (*thing_sector_mask)[portal.destination]=1U;
                            next[portal.destination]=1U;
                        }
                    }
                }
                frontier.swap(next);
            }
        }
        return stats;
    }

    // Native SithThing.sector pointer to our independent reflected
    // sector mask. Unknown/stale addresses always keep the Thing.
    bool thing_sector_visible(uintptr_t sector_ptr,
                              std::vector<uint8_t> const &mask)const
    {
        if(!ready() || mask.size()!=sectors_.size() ||
           sector_table_==0 || sector_ptr<sector_table_)return true;
        uint64_t const delta=uint64_t(sector_ptr)-sector_table_;
        if(delta%0x98U || delta/0x98U>=sectors_.size())return true;
        return mask[size_t(delta/0x98U)]!=0U;
    }

    // The native sector's clipped window applies to geometry as well as
    // traversal. AABB projection is deliberately conservative here: if a
    // face intersects the near plane, retaining it is safer than a hole.
    bool surface_in_window(uint32_t index,
        std::array<float,3> const &lo,std::array<float,3> const &hi,
        gpu_3do_matrix34 const &view,float tan_half_fov,float aspect,
        std::vector<planar_reflection_portal_rect> const &windows)const
    {
        if(index>=surface_owner_.size() || surface_owner_[index]<0 ||
           windows.size()!=sectors_.size())return true;
        auto const window=windows[size_t(surface_owner_[index])];
        auto const projected=planar_reflection_portal_project_aabb(
            lo,hi,view,tan_half_fov,aspect);
        return !planar_reflection_portal_intersect(
            window,projected).empty;
    }

};
} // namespace jkgm
