#pragma once
// Conservative 128x128 sky-aperture overlap mask for static WORLD casters.
// Dynamic 3DOs and the Thing atlas are deliberately outside this filter.
#include "sun_shadow.hpp"
#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <vector>

namespace jkgm {

// Fast opt-in 128x128 sky-first selector for WORLD shadow casters.
// Padded projected rectangles are deliberately conservative. 3DOs and the
// dynamic atlas remain outside this selector. Every malformed candidate
// fails open; the selector cannot discard sky rays hidden behind a roof.
struct sun_sky_caster_fast_mask {
    static constexpr int side=128;
    static constexpr int padding_tiles=2;
    struct rect {
        int x0=0,y0=0,x1=-1,y1=-1;
        bool unknown=false;
        bool intersects() const {return x0<=x1 && y0<=y1;}
    };
    sun_shadow_projection view{};
    std::array<std::array<uint64_t,2>,side> rows{};
    bool valid=false,fail_open=false;

    void reset(sun_shadow_projection const &projection) {
        view=projection;
        rows={};
        valid=view.width>0.0f && view.height>0.0f &&
            std::isfinite(view.width) && std::isfinite(view.height) &&
            decal_finite(view.origin) && decal_finite(view.right) &&
            decal_finite(view.up);
        fail_open=!valid;
        if(!valid)mark_all();
    }

    void mark_all() {
        for(auto &row:rows)row={{~uint64_t(0),~uint64_t(0)}};
        fail_open=true;
    }

    static uint64_t bit_range(int a,int b) {
        if(a>b)return 0U;
        uint64_t const upper=b==63?~uint64_t(0):
            ((uint64_t(1)<<(b+1))-1U);
        uint64_t const lower=a==0?0U:((uint64_t(1)<<a)-1U);
        return upper&~lower;
    }

    rect projected_rect(std::vector<decal_vec3> const &polygon) const {
        if(!valid || polygon.size()<3)
            return {0,0,side-1,side-1,true};
        double lo_x=std::numeric_limits<double>::infinity();
        double lo_y=lo_x,hi_x=-lo_x,hi_y=-lo_y;
        for(auto const &point:polygon) {
            if(!decal_finite(point))
                return {0,0,side-1,side-1,true};
            double const dx=double(point[0])-view.origin[0];
            double const dy=double(point[1])-view.origin[1];
            double const dz=double(point[2])-view.origin[2];
            double const x=(dx*view.right[0]+dy*view.right[1]+
                            dz*view.right[2])/double(view.width);
            double const y=(dx*view.up[0]+dy*view.up[1]+
                            dz*view.up[2])/double(view.height);
            if(!std::isfinite(x)||!std::isfinite(y))
                return {0,0,side-1,side-1,true};
            lo_x=std::min(lo_x,x);hi_x=std::max(hi_x,x);
            lo_y=std::min(lo_y,y);hi_y=std::max(hi_y,y);
        }
        // The sky pass dilates silhouettes by sub-texel offsets. A face
        // just beyond the cascade edge must still mark border tiles
        // after our two-cell halo is applied.
        double const halo=double(padding_tiles)/double(side);
        if(hi_x< -halo || lo_x>1.0+halo ||
           hi_y< -halo || lo_y>1.0+halo)return {};
        auto coord=[](double v) {
            return int(std::floor(std::max(-1.0,std::min(2.0,v))*
                                  double(side)));
        };
        return {std::max(0,coord(lo_x)-padding_tiles),
                std::max(0,coord(lo_y)-padding_tiles),
                std::min(side-1,coord(hi_x)+padding_tiles),
                std::min(side-1,coord(hi_y)+padding_tiles),false};
    }

    void include_polygon(std::vector<decal_vec3> const &polygon) {
        if(fail_open)return;
        auto const r=projected_rect(polygon);
        if(r.unknown){mark_all();return;}
        if(!r.intersects())return;
        uint64_t const low=bit_range(r.x0,std::min(63,r.x1));
        uint64_t const high=r.x1>=64 ?
            bit_range(std::max(0,r.x0-64),r.x1-64):0U;
        for(int y=r.y0;y<=r.y1;++y) {
            rows[size_t(y)][0]|=low;
            rows[size_t(y)][1]|=high;
        }
    }

    bool may_overlap(std::vector<decal_vec3> const &polygon) const {
        if(fail_open || !valid)return true;
        auto const r=projected_rect(polygon);
        if(r.unknown)return true;
        if(!r.intersects())return false;
        uint64_t const low=bit_range(r.x0,std::min(63,r.x1));
        uint64_t const high=r.x1>=64 ?
            bit_range(std::max(0,r.x0-64),r.x1-64):0U;
        for(int y=r.y0;y<=r.y1;++y)
            if((rows[size_t(y)][0]&low)!=0U ||
               (rows[size_t(y)][1]&high)!=0U)
                return true;
        return false;
    }

    uint64_t signature() const {
        uint64_t h=14695981039346656037ULL;
        for(auto const &row:rows)for(uint64_t bits:row) {
            h^=bits;h*=1099511628211ULL;
        }
        return h;
    }

    unsigned occupied_tiles() const {
        unsigned n=0;
        for(auto const &row:rows)for(uint64_t bits:row)
            while(bits!=0U){bits&=bits-1U;++n;}
        return n;
    }
};

} // namespace jkgm
