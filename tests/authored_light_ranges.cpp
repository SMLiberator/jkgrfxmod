#include "renderer/light_intensity_range.hpp"
#include <cassert>
#include <cmath>
#include <limits>

int main()
{
    using jkgm::authored_light_limit;
    using jkgm::clamp_authored_light;
    using jkgm::clamp_authored_extra_light;

    assert(authored_light_limit(false)==1.0f);
    assert(authored_light_limit(true)==2.0f);
    for(bool overbright : {false,true}) {
        float const cap=authored_light_limit(overbright);
        assert(clamp_authored_light(-0.3f,overbright)==0.0f);
        assert(clamp_authored_light(0.8f,overbright)==0.8f);
        assert(clamp_authored_light(9999.0f,overbright)==cap);
        assert(clamp_authored_extra_light(-0.3f,overbright)==-0.3f);
        assert(clamp_authored_extra_light(9999.0f,overbright)==cap);
        // Inputs are limited independently. Never saturate their sum:
        // direct/face/sector light may legitimately yield HDR brightness.
        float const sum=clamp_authored_light(4.0f,overbright)+
                        clamp_authored_extra_light(4.0f,overbright)+0.5f;
        assert(std::abs(sum-(2.0f*cap+0.5f))<0.0001f);
    }
    return 0;
}
