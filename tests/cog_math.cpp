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
    // Reproduces the input from the in-game stack report. A correct
    // COG handler consumes both arguments and pushes 6, not 1.
    assert(eq(cog_math_max(6.0f,1.0f),6.0f));
    assert(eq(cog_math_max(1.0f,6.0f),6.0f));
    assert(eq(cog_math_min(6.0f,1.0f),1.0f));
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
    // Uniform spherical direction generation: cardinal axes and oblique case.
    auto const pos_x=cog_math_rand_dir(0.0f,0.5f);
    auto const pos_y=cog_math_rand_dir(0.25f,0.5f);
    auto const neg_z=cog_math_rand_dir(0.0f,0.0f);
    auto const pos_z=cog_math_rand_dir(0.0f,1.0f);
    assert(eq(pos_x[0],1.0f)&&eq(pos_x[1],0.0f)&&eq(pos_x[2],0.0f));
    assert(eq(pos_y[0],0.0f)&&eq(pos_y[1],1.0f)&&eq(pos_y[2],0.0f));
    assert(eq(neg_z[0],0.0f)&&eq(neg_z[1],0.0f)&&eq(neg_z[2],-1.0f));
    assert(eq(pos_z[0],0.0f)&&eq(pos_z[1],0.0f)&&eq(pos_z[2],1.0f));
    for(int u=0;u<25;++u)for(int v=0;v<25;++v) {
        auto const dir=cog_math_rand_dir((u+0.5f)/25.0f,(v+0.5f)/25.0f);
        float const length=std::sqrt(dir[0]*dir[0]+dir[1]*dir[1]+dir[2]*dir[2]);
        assert(eq(length,1.0f));
    }
    return 0;
}
