#include "renderer/sprite_flare_visibility.hpp"
#include "renderer/sprite_orientation.hpp"
#include <cassert>
#include <cmath>
#include <limits>
using namespace jkgm;
static bool near(float a,float b) {return std::fabs(a-b)<1.e-5f;}
int main() {
    assert(sprite_center_occluded_flare==0x40U);
    // A flare-only type preserves the ordinary camera-facing billboard.
    assert((sprite_center_occluded_flare&sprite_extended_mask)==0U);
    sprite_axes thing{},camera{},axes{};
    assert(compute_sprite_axes(sprite_center_occluded_flare,thing,camera,axes));
    assert(near(axes.look[1],1.0f));
    // A clear center renders its *whole* quad: only the center is tested.
    assert(sprite_flare_center_unoccluded(0.8f,0.4f));
    assert(sprite_flare_center_unoccluded(0.4f,0.4f));
    assert(!sprite_flare_center_unoccluded(0.2f,0.4f));
    assert(sprite_flare_center_unoccluded(0.39995f,0.4f)); // bias
    assert(!sprite_flare_center_unoccluded(-1.0f,0.4f));
    assert(!sprite_flare_center_unoccluded(1.0f,1.2f));
    assert(!sprite_flare_center_unoccluded(
        std::numeric_limits<float>::quiet_NaN(),0.5f));
    sprite_flare_fade fade{};
    assert(near(fade.update(true,0.04f,0.08f),0.5f));
    assert(near(fade.update(true,0.08f,0.08f),1.0f));
    assert(near(fade.update(false,0.04f,0.08f),0.5f));
    assert(near(sprite_effective_opacity(0.4f,fade.visibility),0.2f));
    assert(near(sprite_effective_opacity(4.0f,1.0f),1.0f));
    fade.reset(false);
    assert(near(fade.visibility,0.0f));
}
