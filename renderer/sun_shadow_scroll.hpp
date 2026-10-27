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
    // Physical slot dirty flags survive scrolling until the GPU confirms a
    // successful depth render. Planning a tile does NOT mean it is valid.
    std::vector<std::uint8_t> pending;

    sun_shadow_scroll_grid() = default;
    explicit sun_shadow_scroll_grid(int tiles):count(tiles) {}

    static int wrap(int n,int modulus) {
        int const r=n%modulus;
        return r<0?r+modulus:r;
    }

    void invalidate() { valid=false; pending.clear(); }

    bool in_window(int wx,int wy) const {
        return valid && wx>=origin_x && wx-origin_x<count &&
            wy>=origin_y && wy-origin_y<count;
    }

    std::size_t slot_index(int atlas_x,int atlas_y) const {
        return std::size_t(atlas_y)*std::size_t(count)+std::size_t(atlas_x);
    }

    // Only call this AFTER successfully drawing and storing the tile depth.
    // A stale completion from an earlier grid position must not validate
    // a recycled physical slot assigned to a different world tile.
    bool mark_rendered(sun_shadow_scroll_tile const &tile) {
        if(!in_window(tile.world_x,tile.world_y))return false;
        auto const current=physical_slot(tile.world_x,tile.world_y);
        if(tile.atlas_x!=current.atlas_x || tile.atlas_y!=current.atlas_y)
            return false;
        auto const slot=slot_index(tile.atlas_x,tile.atlas_y);
        if(slot>=pending.size())return false;
        pending[slot]=0;
        return true;
    }

    std::vector<sun_shadow_scroll_tile> pending_tiles(
        std::size_t budget=std::numeric_limits<std::size_t>::max()) const
    {
        std::vector<sun_shadow_scroll_tile> result;
        if(!valid)return result;
        for(int y=origin_y;y<origin_y+count;++y)
            for(int x=origin_x;x<origin_x+count;++x) {
                auto const tile=physical_slot(x,y);
                auto const slot=slot_index(tile.atlas_x,tile.atlas_y);
                if(slot<pending.size() && pending[slot]) {
                    result.push_back(tile);
                    if(result.size()>=budget)return result;
                }
            }
        return result;
    }

    // Invalidate only sunlight-ray columns intersecting a geometry change.
    // Call for BOTH old and new bounds of a moved or GeoMode-changed caster.
    // Values are absolute light-space coordinates (not world X/Y).
    std::vector<sun_shadow_scroll_tile> invalidate_light_rect(
        double min_x,double max_x,double min_y,double max_y)
    {
        std::vector<sun_shadow_scroll_tile> added;
        if(!valid)return added;
        if(!std::isfinite(min_x) || !std::isfinite(max_x) ||
           !std::isfinite(min_y) || !std::isfinite(max_y) ||
           min_x>max_x || min_y>max_y) {
            // Unsafe geometry bounds must never leave an apparently valid
            // cached image: conservatively invalidate every atlas slot.
            for(int row=0;row<count;++row)
                for(int col=0;col<count;++col) {
                    auto const tile=physical_slot(origin_x+col,origin_y+row);
                    auto const slot=slot_index(tile.atlas_x,tile.atlas_y);
                    if(slot<pending.size() && !pending[slot]) {
                        pending[slot]=1;
                        added.push_back(tile);
                    }
                }
            return added;
        }
        double const first_x=double(origin_x)*tile_units;
        double const first_y=double(origin_y)*tile_units;
        double const last_x=first_x+double(count)*tile_units;
        double const last_y=first_y+double(count)*tile_units;
        if(max_x<first_x || min_x>last_x ||
           max_y<first_y || min_y>last_y)return added;
        // Include touching neighbors: the actual depth rasterizer and PCF
        // have nonzero footprints on tile borders.
        int const lo_x=std::max(origin_x,int(std::floor(
            std::max(min_x,first_x)/tile_units))-1);
        int const hi_x=std::min(origin_x+count-1,int(std::floor(
            std::min(max_x,last_x-1.0e-9)/tile_units))+1);
        int const lo_y=std::max(origin_y,int(std::floor(
            std::max(min_y,first_y)/tile_units))-1);
        int const hi_y=std::min(origin_y+count-1,int(std::floor(
            std::min(max_y,last_y-1.0e-9)/tile_units))+1);
        for(int row=lo_y;row<=hi_y;++row)
            for(int col=lo_x;col<=hi_x;++col) {
                auto const tile=physical_slot(col,row);
                auto const slot=slot_index(tile.atlas_x,tile.atlas_y);
                if(slot<pending.size() && !pending[slot]) {
                    pending[slot]=1;
                    added.push_back(tile);
                }
            }
        return added;
    }

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
            pending.assign(std::size_t(count)*std::size_t(count),1U);
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
                sun_shadow_scroll_tile const tile{
                    wx,wy,wrap(col+ring_x,count),wrap(row+ring_y,count)};
                dirty.push_back(tile);
                if(slot_index(tile.atlas_x,tile.atlas_y)<pending.size())
                    pending[slot_index(tile.atlas_x,tile.atlas_y)]=1U;
            }
        return dirty;
    }

    sun_shadow_scroll_tile physical_slot(int wx,int wy) const {
        return {wx,wy,wrap(wx-origin_x+ring_x,count),
                      wrap(wy-origin_y+ring_y,count)};
    }
};

// Reconstruct a conventional, camera-fitted depth image from circular
// sunlight depth tiles. The read blits can be scaled (GL_NEAREST); depth
// values remain comparable because all tiles have the same light-space Z.
// This is the first GPU implementation; direct tiled shader sampling will
// eventually eliminate these full-resolution depth reconstructions.
struct sun_shadow_scroll_blit {
    int source_x0=0,source_y0=0,source_x1=0,source_y1=0;
    int target_x0=0,target_y0=0,target_x1=0,target_y1=0;
};

inline std::vector<sun_shadow_scroll_blit> sun_shadow_scroll_blits(
    sun_shadow_scroll_grid const &grid,
    double view_min_x,double view_min_y,double view_width,double view_height,
    int resolution)
{
    std::vector<sun_shadow_scroll_blit> result;
    if(!grid.valid || grid.count<2 || resolution<grid.count ||
       resolution%grid.count!=0 || !(view_width>0.0) ||
       !(view_height>0.0) || !(grid.tile_units>0.0) ||
       !std::isfinite(view_min_x) || !std::isfinite(view_min_y) ||
       !std::isfinite(view_width) || !std::isfinite(view_height))
        return result;
    double const units=grid.tile_units;
    double const x0=double(grid.origin_x)*units;
    double const y0=double(grid.origin_y)*units;
    double const x1=x0+double(grid.count)*units;
    double const y1=y0+double(grid.count)*units;
    // A missing portion of the depth image must not be silently treated as
    // sunlight. Fall back to the legacy complete cascade in that case.
    double const eps=units*1.0e-5;
    if(view_min_x<x0-eps || view_min_y<y0-eps ||
       view_min_x+view_width>x1+eps ||
       view_min_y+view_height>y1+eps)return result;
    int const tile_pixels=resolution/grid.count;
    auto coord=[](double fraction,int dimension) {
        return std::max(0,std::min(dimension,
            int(std::lround(fraction*double(dimension)))));
    };
    for(int row=grid.origin_y;row<grid.origin_y+grid.count;++row)
        for(int col=grid.origin_x;col<grid.origin_x+grid.count;++col) {
            double const min_x=std::max(view_min_x,double(col)*units);
            double const min_y=std::max(view_min_y,double(row)*units);
            double const max_x=std::min(view_min_x+view_width,
                                        double(col+1)*units);
            double const max_y=std::min(view_min_y+view_height,
                                        double(row+1)*units);
            if(!(max_x>min_x && max_y>min_y))continue;
            auto const slot=grid.physical_slot(col,row);
            int const dx0=coord((min_x-view_min_x)/view_width,resolution);
            int const dy0=coord((min_y-view_min_y)/view_height,resolution);
            int const dx1=coord((max_x-view_min_x)/view_width,resolution);
            int const dy1=coord((max_y-view_min_y)/view_height,resolution);
            int const sx0=slot.atlas_x*tile_pixels+
                coord((min_x-double(col)*units)/units,tile_pixels);
            int const sy0=slot.atlas_y*tile_pixels+
                coord((min_y-double(row)*units)/units,tile_pixels);
            int const sx1=slot.atlas_x*tile_pixels+
                coord((max_x-double(col)*units)/units,tile_pixels);
            int const sy1=slot.atlas_y*tile_pixels+
                coord((max_y-double(row)*units)/units,tile_pixels);
            if(dx1<=dx0 || dy1<=dy0 || sx1<=sx0 || sy1<=sy0)continue;
            result.push_back({sx0,sy0,sx1,sy1,dx0,dy0,dx1,dy1});
        }
    return result;
}

} // namespace jkgm
