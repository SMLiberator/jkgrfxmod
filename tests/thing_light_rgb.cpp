#include "renderer/thing_light_rgb.hpp"
#include <cassert>
#include <cmath>
#include <limits>
using namespace jkgm::thing_light_rgb;
static bool near(float a,float b){return std::fabs(a-b)<0.0001f;}
int main()
{
    rgb x{};
    assert(normalize({{0.f,0.f,0.f}},x)&&x==white);
    assert(normalize({{1.f,1.f,1.f}},x)&&x==white);
    assert(normalize({{0.4f,0.2f,0.f}},x));
    assert(near(x[0],1.f)&&near(x[1],0.5f)&&near(x[2],0.f));
    assert(!normalize({{1.1f,0.f,0.f}},x));
    assert(!normalize({{-0.01f,0.f,0.f}},x));
    assert(!normalize({{std::numeric_limits<float>::quiet_NaN(),0.f,0.f}},x));
    assert(!normalize({{std::numeric_limits<float>::infinity(),0.f,0.f}},x));
    assert(lerp({{1,0,0}},{{0,0,1}},0.5f)==(rgb{{0.5f,0.f,0.5f}}));
    assert(lerp({{1,0,0}},{{0,0,1}},-1.f)==(rgb{{1.f,0.f,0.f}}));

    registry r;
    r.prepare_world(0x100000U);
    assert(r.set(0x110000U,0U,true,{{0.6f,0.3f,0.f}},0.0,10.0));
    assert(r.get(0x110000U,0U,x,10.0));
    assert(near(x[0],1.f)&&near(x[1],0.5f)); // template normalized
    r.inherit(0x120000U,42U,0x110000U,12.0); // spawn from template
    assert(r.get(0x120000U,42U,x,12.0));
    assert(near(x[0],1.f)&&near(x[1],0.5f));
    assert(!r.get(0x120000U,43U,x,12.0)&&x==white); // reused slot
    assert(r.set(0x120000U,42U,false,{{0,0,1}},2.0,20.0));
    assert(r.get(0x120000U,42U,x,20.0));
    assert(near(x[0],1.f)&&near(x[1],0.5f)&&near(x[2],0.f));
    assert(r.get(0x120000U,42U,x,21.0));
    assert(near(x[0],0.5f)&&near(x[1],0.25f)&&near(x[2],0.5f));
    assert(r.get(0x120000U,42U,x,22.0));
    assert(x==(rgb{{0.f,0.f,1.f}}));
    // Readout during the fade matches what the shader receives; no
    // per-frame renormalization to preserve smooth interpolation.
    assert(r.set(0x120000U,42U,false,{{1,0,0}},2.0,22.0));
    auto entries=r.things(23.0);
    assert(entries.size()==1U);
    assert(entries[0].address==0x120000U);
    assert(entries[0].signature==42U);
    assert(entries[0].to==(rgb{{0.5f,0.f,0.5f}}));
    // Zero means white; only SetThingLight(0) actually extinguishes a light.
    assert(r.set(0x120000U,42U,false,{{0,0,0}},0.0,25.0));
    assert(r.get(0x120000U,42U,x,25.0)&&x==white);
    assert(r.things(25.0).empty());
    // During clone, the new owner must not inherit a recycled Thing's color.
    r.inherit(0x120000U,43U,0x100100U,26.0);
    assert(!r.get(0x120000U,43U,x,26.0)&&x==white);
    assert(!r.set(0x120000U,43U,false,{{1,0,0}},-1,30));
    assert(!r.set(0x120000U,43U,false,{{1,0,0}},3601,30));
    assert(!r.set(0x120000U,43U,false,{{1,0,0}},0,
                  std::numeric_limits<double>::quiet_NaN()));
    r.prepare_world(0x200000U);
    assert(!r.get(0x110000U,0U,x,30.0));
    assert(!r.get(0x120000U,43U,x,30.0));
    r.clear();
    assert(r.things(30.0).empty());
    return 0;
}
