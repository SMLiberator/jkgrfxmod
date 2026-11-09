#pragma once
// Phase 1 of sky-first sun shadows: conservative CPU-only *measurements*.
// A 32x32 light-space tile mask bounds areas beneath authored sky polygons.
// This currently NEVER changes caster submission, depth maps or shading.
// XY projection alone cannot tell whether a roof blocks the sky, so a tile
// is only a possible sunlight region, not proof of actual exposure.
#include "sun_shadow.hpp"
#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <vector>

namespace jkgm {

struct sun_sky_caster_coverage {
    static constexpr int grid_side=32;
    static constexpr int tile_count=grid_side*grid_side;
    struct tile_rect {
        int x0=0,y0=0,x1=-1,y1=-1;
        bool unknown=false;
        bool intersects() const {return x0<=x1 && y0<=y1;}
    };
    sun_shadow_projection view{};
    std::array<uint8_t,tile_count> tiles{};
    unsigned active_tiles=0;
    bool ready=false;
    bool fail_open=false;

    void reset(sun_shadow_projection const &projection)
    {
        view=projection;
        tiles.fill(0U);
        active_tiles=0;
        fail_open=false;
        ready=view.width>0.0f && view.height>0.0f &&
            std::isfinite(view.width) && std::isfinite(view.height) &&
            decal_finite(view.origin) && decal_finite(view.right) &&
            decal_finite(view.up);
        if(!ready)mark_all();
    }

    // Never exclude a candidate based on malformed/unreadable sky geometry.
    void mark_all()
    {
        tiles.fill(1U);
        active_tiles=tile_count;
        fail_open=true;
    }

    tile_rect projected_bounds(decal_vec3 const *points,size_t count,
                               int padding_tiles=1) const
    {
        tile_rect r{};
        if(!ready)return {0,0,grid_side-1,grid_side-1,true};
        if(count==0)return r;
        double min_x=std::numeric_limits<double>::infinity();
        double min_y=min_x,max_x=-min_x,max_y=-min_x;
        for(size_t i=0;i<count;++i) {
            if(!decal_finite(points[i]))
                return {0,0,grid_side-1,grid_side-1,true};
            double const dx=double(points[i][0])-view.origin[0];
            double const dy=double(points[i][1])-view.origin[1];
            double const dz=double(points[i][2])-view.origin[2];
            double const x=(dx*view.right[0]+dy*view.right[1]+
                            dz*view.right[2])/double(view.width);
            double const y=(dx*view.up[0]+dy*view.up[1]+
                            dz*view.up[2])/double(view.height);
            if(!std::isfinite(x) || !std::isfinite(y))
                return {0,0,grid_side-1,grid_side-1,true};
            min_x=std::min(min_x,x);max_x=std::max(max_x,x);
            min_y=std::min(min_y,y);max_y=std::max(max_y,y);
        }
        // Reject completely off-map polygons before casting to integers:
        // sky/model coordinates can be arbitrarily far from a cascade.
        if(max_x<0.0 || min_x>1.0 || max_y<0.0 || min_y>1.0)
            return r;
        auto coord=[&](double v) {
            return int(std::floor(std::max(-1.0,std::min(2.0,v))*
                                  double(grid_side)));
        };
        r.x0=std::max(0,coord(min_x)-padding_tiles);
        r.y0=std::max(0,coord(min_y)-padding_tiles);
        r.x1=std::min(grid_side-1,coord(max_x)+padding_tiles);
        r.y1=std::min(grid_side-1,coord(max_y)+padding_tiles);
        return r;
    }

    void include_polygon(std::vector<decal_vec3> const &polygon)
    {
        if(polygon.size()<3)return;
        auto const r=projected_bounds(polygon.data(),polygon.size());
        if(r.unknown) {mark_all();return;}
        if(!r.intersects())return;
        for(int y=r.y0;y<=r.y1;++y)
            for(int x=r.x0;x<=r.x1;++x) {
                auto &tile=tiles[size_t(y*grid_side+x)];
                if(tile==0U){tile=1U;++active_tiles;}
            }
    }

    bool intersects_polygon(std::vector<decal_vec3> const &polygon) const
    {
        if(fail_open || !ready)return true;
        if(polygon.size()<3)return true; // unknown bounds: fail open
        return intersects(projected_bounds(polygon.data(),polygon.size()));
    }

    bool intersects_aabb(decal_volume_aabb const &bounds) const
    {
        if(fail_open || !ready)return true;
        if(!decal_finite(bounds.low) || !decal_finite(bounds.high))
            return true;
        for(int i=0;i<3;++i)
            if(bounds.low[i]>bounds.high[i])return true;
        std::array<decal_vec3,8> corners{};
        for(int c=0;c<8;++c)
            corners[size_t(c)]={{
                (c&1)?bounds.high[0]:bounds.low[0],
                (c&2)?bounds.high[1]:bounds.low[1],
                (c&4)?bounds.high[2]:bounds.low[2]}};
        return intersects(projected_bounds(corners.data(),corners.size()));
    }

private:
    bool intersects(tile_rect const &r) const
    {
        if(r.unknown)return true;
        if(!r.intersects())return false;
        for(int y=r.y0;y<=r.y1;++y)
            for(int x=r.x0;x<=r.x1;++x)
                if(tiles[size_t(y*grid_side+x)]!=0U)return true;
        return false;
    }
};

} // namespace jkgm
