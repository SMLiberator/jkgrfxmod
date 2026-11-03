#include "renderer/saber_light.hpp"
#include <cassert>
#include <cmath>
#include <limits>
using namespace jkgm::saber_light;
static bool near(float a,float b) {return std::fabs(a-b)<0.0001f;}
int main() {
    registry r;
    float adjusted=-1;
    assert(near(scale_intensity(0.6f,0.6f),0.6f));
    assert(near(scale_intensity(0.3f,0.3f),0.15f));
    assert(near(scale_intensity(0.3f,0.0f),0.0f));
    assert(near(scale_intensity(0.0f,16.0f),0.0f));
    assert(!r.set(0x10000,7,-1.0f,0.3f,adjusted));
    assert(!r.set(0x10000,7,17.0f,0.3f,adjusted));
    assert(!r.set(0x10000,7,std::numeric_limits<float>::quiet_NaN(),
                  0.3f,adjusted));
    assert(!r.set(0,7,0.0f,0.3f,adjusted));
    assert(!r.set(0x10000,0,0.0f,0.3f,adjusted));
    r.prepare_world(10);
    assert(r.set(0x10000,7,0.0f,0.3f,adjusted) && near(adjusted,0));
    record saved{};
    assert(r.get(0x10000,7,saved) && near(saved.unscaled_intensity,0.3f));
    assert(r.capture_native(0x10000,7,0.5f,adjusted) && near(adjusted,0));
    assert(r.set(0x10000,7,0.3f,0.0f,adjusted));
    assert(near(adjusted,0.25f)); // Preserve original, not scaled zero.
    assert(r.get(0x10000,7,saved) && near(saved.unscaled_intensity,0.5f));
    assert(r.set(0x10000,7,vanilla_max,0.25f,adjusted));
    assert(near(adjusted,0.5f) && r.empty()); // Restore native.
    assert(r.set(0x10000,7,1.2f,0.6f,adjusted));
    assert(near(adjusted,1.2f));
    assert(!r.get(0x10000,8,saved)); // Recycled Thing identity.
    assert(r.set(0x10000,8,0.0f,0.6f,adjusted));
    assert(r.get(0x10000,8,saved) && near(saved.unscaled_intensity,0.6f));
    r.prepare_world(10);
    assert(!r.empty());
    r.prepare_world(11);
    assert(r.empty()); // Never transfer an override to a new level.
    assert(r.set(0x10000,9,0.0f,0.1f,adjusted));
    r.erase(0x10000);
    assert(r.empty());
    return 0;
}
