#pragma once
// GL-free packing and texel-density selection for dynamic sunlight casters.
#include <algorithm>
#include <cstddef>
#include <cmath>
#include <vector>
namespace jkgm {
constexpr int sun_dynamic_min_tile=64;
struct sun_dynamic_shadow_tile {
    int x=0,y=0,size=0; // Pixel rectangle including a 2px sampling guard
};
inline unsigned choose_sun_dynamic_shadow_tile(float projected_pixels,
                                                unsigned legacy_resolution)
{
    unsigned const limit=std::max(64U,std::min(1024U,legacy_resolution*2U));
    unsigned size=64U;
    if(!std::isfinite(projected_pixels))return size;
    while(size<limit && float(size)<projected_pixels)size*=2U;
    return std::min(size,limit);
}
// Limit a large near-camera Thing so subsequent candidates can retain at
// least 64x64 texels. The atlas layout may further shrink to fit geometry.
inline unsigned sun_dynamic_shadow_budget_tile(
    unsigned requested,std::size_t used_pixels,std::size_t atlas_pixels,
    std::size_t remaining_things)
{
    unsigned size=std::max(64U,requested);
    std::size_t const reserve=remaining_things*64U*64U;
    while(size>64U &&
          used_pixels+std::size_t(size)*size+reserve>atlas_pixels)
        size/=2U;
    return size;
}

class sun_dynamic_shadow_layout {
    int width_,height_;
    std::vector<sun_dynamic_shadow_tile> placed_;
public:
    sun_dynamic_shadow_layout(int width,int height)
        :width_(width),height_(height) {}
    // Try the requested power-of-two tile; shrink if the atlas is occupied.
    bool allocate(int requested,sun_dynamic_shadow_tile &out)
    {
        int size=std::max(sun_dynamic_min_tile,requested);
        for(;size>=sun_dynamic_min_tile;size/=2) {
            for(int y=0;y+size<=height_;y+=sun_dynamic_min_tile) {
                for(int x=0;x+size<=width_;x+=sun_dynamic_min_tile) {
                    bool blocked=false;
                    for(auto const &r:placed_) {
                        if(x<r.x+r.size && x+size>r.x &&
                           y<r.y+r.size && y+size>r.y) {
                            blocked=true;break;
                        }
                    }
                    if(!blocked) {
                        out={x,y,size};
                        placed_.push_back(out);
                        return true;
                    }
                }
            }
        }
        return false;
    }
    // Material rejection must not reserve a tile.
    void undo_last() { if(!placed_.empty())placed_.pop_back(); }
};
} // namespace jkgm
