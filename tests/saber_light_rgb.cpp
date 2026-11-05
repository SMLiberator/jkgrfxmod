#include "renderer/saber_light_rgb.hpp"
#include <cassert>
#include <cmath>
#include <limits>
using namespace jkgm::saber_light_rgb;
static bool eq(float a,float b){return std::fabs(a-b)<1.e-5f;}
int main()
{
    auto red=from_palette(190,22,10);
    assert(eq(red[0],1.f)&&eq(red[1],22.f/190.f));
    assert(colored(red));
    auto blue=from_palette(0,0,255);
    assert(colored(blue) && eq(blue[2],1.f));
    assert(!colored(from_palette(255,255,255)));
    assert(from_palette(0,0,0)==white);
    assert(!colored({{2.f,0.f,0.f}}));
    assert(!colored({{std::numeric_limits<float>::quiet_NaN(),0.f,0.f}}));

    camera_light lights[3]{
        {0U,{{7.f,8.f,9.f}},0.2f},
        {4U,{{1.f,2.f,3.f}},0.6f},
        {5U,{{1.2f,2.f,3.f}},0.6f}
    };
    uint32_t id=55U;
    assert(find_unique_native_light(lights,3,{{1.f,2.f,3.f}},0.6f,id));
    assert(id==4U);
    assert(!find_unique_native_light(lights,3,{{1.f,2.f,3.f}},0.2f,id));
    assert(!find_unique_native_light(lights,3,{{1.f,2.f,3.f}},0.0f,id));
    assert(!find_unique_native_light(lights,3,{{1.f,2.f,3.f}},-1.f,id));
    // Same location/strength from a different Thing => no accidental tint.
    auto coincident=lights[1];
    coincident.id=6;
    camera_light ambiguous[]{lights[1],coincident};
    assert(!find_unique_native_light(ambiguous,2,{{1.f,2.f,3.f}},0.6f,id));
    assert(!find_unique_native_light(lights,3,{{7.f,8.f,9.f}},0.6f,id));
    registry pending;
    assert(pending.consume().get(4)==white);
    pending.add(4,red);
    pending.add(5,blue);
    auto frame=pending.consume();
    assert(frame.valid[4] && frame.valid[5]);
    assert(frame.get(4)==red);
    assert(frame.get(5)==blue);
    assert(frame.get(0)==white);
    assert(pending.consume().get(4)==white); // frame reset
    pending.add(99,red);
    assert(pending.consume().get(4)==white);
    pending.add(4,white); // no-op; never turn non-saber white
    assert(!pending.consume().valid[4]);
    pending.add(4,red);
    pending.clear(); // renderer/world shutdown
    assert(!pending.consume().valid[4]);
    return 0;
}
