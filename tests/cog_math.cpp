#include "renderer/cog_math.hpp"
#include <cassert>
#include <cmath>
#include <limits>

using namespace jkgm;

static bool eq(float a,float b) { return std::fabs(a-b)<0.00001f; }

int main()
{
    assert(eq(cog_math_min(3.5f,7.0f),3.5f));
    assert(eq(cog_math_min(-5.0f,-2.0f),-5.0f));
    assert(eq(cog_math_max(3.5f,7.0f),7.0f));
    assert(eq(cog_math_max(-5.0f,-2.0f),-2.0f));
    assert(eq(cog_math_min(4.0f,4.0f),4.0f));
    assert(eq(cog_math_max(4.0f,4.0f),4.0f));

    assert(eq(cog_math_clamp(-5.0f,0.0f,10.0f),0.0f));
    assert(eq(cog_math_clamp(12.0f,0.0f,10.0f),10.0f));
    assert(eq(cog_math_clamp(4.0f,0.0f,10.0f),4.0f));
    // Reversed bounds normalize instead of invoking undefined std::clamp.
    assert(eq(cog_math_clamp(12.0f,10.0f,0.0f),10.0f));
    assert(eq(cog_math_clamp(4.0f,10.0f,0.0f),4.0f));
    assert(eq(cog_math_clamp(-12.0f,10.0f,0.0f),0.0f));
    assert(eq(cog_math_clamp(8.0f,2.0f,2.0f),2.0f));

    assert(cog_math_sign(-0.0001f)==-1);
    assert(cog_math_sign(0.0f)==0);
    assert(cog_math_sign(-0.0f)==0);
    assert(cog_math_sign(0.0001f)==1);
    assert(cog_math_sign(std::numeric_limits<float>::quiet_NaN())==0);

    assert(eq(cog_math_lerp(10.0f,20.0f,0.0f),10.0f));
    assert(eq(cog_math_lerp(10.0f,20.0f,0.5f),15.0f));
    assert(eq(cog_math_lerp(10.0f,20.0f,1.0f),20.0f));
    // Intentionally UNCLAMPED: negative t and t>1 extrapolate.
    assert(eq(cog_math_lerp(10.0f,20.0f,-0.5f),5.0f));
    assert(eq(cog_math_lerp(10.0f,20.0f,1.5f),25.0f));
    assert(eq(cog_math_lerp(20.0f,10.0f,1.5f),5.0f));
    return 0;
}
