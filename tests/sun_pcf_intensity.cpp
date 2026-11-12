#include "renderer/sun_pcf_intensity.hpp"
#include <cassert>
#include <cmath>
#include <limits>

static bool near(float a,float b) { return std::abs(a-b)<1e-5f; }
int main()
{
    using jkgm::sun_intensity_pcf_radii;
    auto const unchanged=sun_intensity_pcf_radii(1.0f,1.5f,0.5f,0.0f,4.0f);
    assert(near(unchanged.near_radius,1.0f));
    assert(near(unchanged.far_radius,1.5f));
    auto const full=sun_intensity_pcf_radii(1.0f,1.5f,4.0f,1.0f,4.0f);
    assert(near(full.near_radius,1.0f));
    assert(near(full.far_radius,1.5f));
    auto const half=sun_intensity_pcf_radii(1.0f,1.5f,2.0f,1.0f,4.0f);
    assert(near(half.near_radius,1.5f));
    assert(near(half.far_radius,2.25f));
    auto const dim=sun_intensity_pcf_radii(1.0f,1.5f,0.0f,1.0f,4.0f);
    assert(near(dim.near_radius,2.0f));
    assert(near(dim.far_radius,3.0f));
    auto const capped=sun_intensity_pcf_radii(3.0f,4.0f,0.0f,2.0f,1.0f);
    assert(near(capped.near_radius,4.0f));
    assert(near(capped.far_radius,4.0f));
    auto const zero=sun_intensity_pcf_radii(0.0f,0.0f,0.5f,2.0f,1.0f);
    assert(near(zero.near_radius,0.0f));
    assert(near(zero.far_radius,0.0f));
    auto const nan=sun_intensity_pcf_radii(
        1.0f,2.0f,std::numeric_limits<float>::quiet_NaN(),1.0f,4.0f);
    assert(near(nan.near_radius,1.0f));
    assert(near(nan.far_radius,2.0f));
    return 0;
}
