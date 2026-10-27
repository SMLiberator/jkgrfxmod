#pragma once
// CPU-only planning for a future circular, light-space sunlight shadow atlas.
// This deliberately DOES NOT manipulate live depth maps: the GPU pass must
// use matching tile transforms, wrapped sampling and guarded PCF first.
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <limits>
#include <vector>

namespace jkgm {

struct sun_shadow_scroll_tile {
    int world_x=0,world_y=0; // persistent light-space grid address
    int atlas_x=0,atlas_y=0; // physical circular atlas slot
};

struct sun_shadow_scroll_grid {
    int count=16;
    int origin_x=0,origin_y=0;
    int ring_x=0,ring_y=0;
    double tile_units=2.0;
    std::uint64_t revision=0;
    bool valid=false;

    explicit sun_shadow_scroll_grid(int tiles=16):count(tiles) {}

    static int wrap(int n,int modulus) {
        int const r=n%modulus;
        return r<0?r+modulus:r;
    }

    void invalidate() { valid=false; }

    // Return only tiles whose PHYSICAL atlas slots need depth regeneration.
    // For 16x16, shifting one grid cell draws only 16 tiles, not 256.
    // At negative world positions use floor, never truncated division.
    std::vector<sun_shadow_scroll_tile> update(
        double center_x,double center_y,double cell_units,
        std::uint64_t new_revision)
    {
        std::vector<sun_shadow_scroll_tile> dirty;
        if(count<2 || count>64 || !(cell_units>0.0) ||
           !std::isfinite(cell_units) ||
           !std::isfinite(center_x) || !std::isfinite(center_y))
            return dirty;
        double const x=std::floor(center_x/cell_units);
        double const y=std::floor(center_y/cell_units);
        constexpr double lo=double(std::numeric_limits<int>::min())+128.0;
        constexpr double hi=double(std::numeric_limits<int>::max())-128.0;
        if(x<lo || x>hi || y<lo || y>hi)return dirty;
        int const wanted_x=int(x)-count/2;
        int const wanted_y=int(y)-count/2;
        bool const new_layout=!valid || revision!=new_revision ||
            std::fabs(tile_units-cell_units)>1.0e-6;
        int const dx=new_layout?count:wanted_x-origin_x;
        int const dy=new_layout?count:wanted_y-origin_y;
        bool const full=new_layout || std::abs(dx)>=count ||
            std::abs(dy)>=count;
        int const old_x=origin_x,old_y=origin_y;
        if(full) {
            ring_x=0;
            ring_y=0;
        } else {
            ring_x=wrap(ring_x+dx,count);
            ring_y=wrap(ring_y+dy,count);
        }
        origin_x=wanted_x;
        origin_y=wanted_y;
        tile_units=cell_units;
        revision=new_revision;
        valid=true;
        if(!full && dx==0 && dy==0)return dirty;
        dirty.reserve(full?std::size_t(count)*std::size_t(count):
                      std::size_t(count)*(std::abs(dx)+std::abs(dy)));
        for(int row=0;row<count;++row)
            for(int col=0;col<count;++col) {
                int const wx=origin_x+col,wy=origin_y+row;
                bool const cached=!full &&
                    wx>=old_x && wx<old_x+count &&
                    wy>=old_y && wy<old_y+count;
                if(cached)continue;
                dirty.push_back({wx,wy,wrap(col+ring_x,count),
                                       wrap(row+ring_y,count)});
            }
        return dirty;
    }

    sun_shadow_scroll_tile physical_slot(int wx,int wy) const {
        return {wx,wy,wrap(wx-origin_x+ring_x,count),
                      wrap(wy-origin_y+ring_y,count)};
    }
};

} // namespace jkgm
