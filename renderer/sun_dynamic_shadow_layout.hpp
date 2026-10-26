#pragma once
// GL-free 64px-step packing and texel-density selection for sunlight casters.
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
    unsigned const limit=(std::max(64U,
        std::min(1024U,legacy_resolution*2U))/64U)*64U;
    if(!std::isfinite(projected_pixels) || projected_pixels<=64.0f)
        return 64U;
    float const bounded=std::min(projected_pixels,float(limit));
    unsigned const stepped=static_cast<unsigned>(std::ceil(bounded/64.0f))*64U;
    return std::min(stepped,limit);
}
// Limit a large near-camera Thing so subsequent candidates can retain at
// least 64x64 texels. The atlas layout may further shrink to fit geometry.
inline unsigned sun_dynamic_shadow_budget_tile(
    unsigned requested,std::size_t used_pixels,std::size_t atlas_pixels,
    std::size_t remaining_things)
{
    unsigned size=std::max(64U,((requested+63U)/64U)*64U);
    std::size_t const reserve=remaining_things*64U*64U;
    while(size>64U &&
          used_pixels+std::size_t(size)*size+reserve>atlas_pixels)
        size-=64U;
    return size;
}

class sun_dynamic_shadow_layout {
    int width_,height_;
    std::vector<sun_dynamic_shadow_tile> placed_;
public:
    sun_dynamic_shadow_layout(int width,int height)
        :width_(width),height_(height) {}
    // Try requested 64-pixel-step tile; shrink if the atlas is occupied.
    bool allocate(int requested,sun_dynamic_shadow_tile &out)
    {
        int size=std::max(sun_dynamic_min_tile,
                          ((std::max(requested,64)+63)/64)*64);
        for(;size>=sun_dynamic_min_tile;size-=sun_dynamic_min_tile) {
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
