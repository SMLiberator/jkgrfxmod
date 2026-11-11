#include "renderer/light_intensity_range.hpp"
#include <cassert>
#include <cmath>
#include <limits>

int main()
{
    using jkgm::authored_light_limit;
    using jkgm::clamp_authored_light;
    using jkgm::clamp_authored_extra_light;
    using jkgm::clamp_authored_baked_sum;
    using jkgm::clamp_authored_extra_floor;
    using jkgm::compose_authored_world_brightness;

    assert(authored_light_limit(false)==1.0f);
    assert(authored_light_limit(true)==2.0f);
    for(bool overbright : {false,true}) {
        float const cap=authored_light_limit(overbright);
        assert(clamp_authored_light(-0.3f,overbright)==0.0f);
        assert(clamp_authored_light(0.8f,overbright)==0.8f);
        assert(clamp_authored_light(9999.0f,overbright)==cap);
        assert(clamp_authored_extra_light(-0.3f,overbright)==-0.3f);
        assert(clamp_authored_extra_light(9999.0f,overbright)==cap);
        // The combined static vertex + face ExtraLight is itself capped.
        assert(clamp_authored_baked_sum(4.0f,4.0f,overbright)==cap);
        assert(clamp_authored_extra_floor(4.0f,4.0f,overbright)==cap);
        assert(clamp_authored_baked_sum(0.0f,-0.5f,overbright)==0.0f);
        assert(std::abs(clamp_authored_baked_sum(0.8f,-0.5f,overbright)-
                        0.3f)<0.0001f);
        // Off is exact native saturation; on preserves additional dynamic HDR.
        assert(compose_authored_world_brightness(cap,0.5f,0.0f,overbright)==
               (overbright?cap+0.5f:1.0f));
        assert(compose_authored_world_brightness(0.0f,0.0f,cap,overbright)==cap);
        assert(compose_authored_world_brightness(0.1f,0.0f,0.3f,overbright)==0.3f);
    }
    return 0;
}
