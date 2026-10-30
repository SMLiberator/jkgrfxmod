#include "../renderer/cog_raycast_head.hpp"
#include <array>
#include <cassert>
#include <cmath>
#include <limits>

static bool near(float a,float b,float eps=0.0002f)
{return std::fabs(a-b)<eps;}

int main()
{
    using jkgm::cog_raycast_head_local_offset;
    std::array<float,3> out{};
    std::array<float,3> const forward{{0,4,0}};
    std::array<float,3> const zero{{0,0,0}};

    // Non-actors (weapons, ghosts, particle Things, etc.) have no headPYR.
    assert(cog_raycast_head_local_offset(3U,forward,{{45,30,10}},out));
    assert(near(out[0],0) && near(out[1],4) && near(out[2],0));

    // Actor and player with zero head angles retain their original direction.
    for(unsigned kind:{2U,10U}) {
        assert(cog_raycast_head_local_offset(kind,forward,zero,out));
        assert(near(out[0],0) && near(out[1],4) && near(out[2],0));
        // Positive pitch elevates head Look; negative pitch points downward.
        assert(cog_raycast_head_local_offset(kind,forward,{{45,0,0}},out));
        assert(near(out[0],0) && near(out[1],2.828427f) &&
               near(out[2],2.828427f));
        assert(cog_raycast_head_local_offset(kind,forward,{{-45,0,0}},out));
        assert(near(out[0],0) && near(out[1],2.828427f) &&
               near(out[2],-2.828427f));
        // Relative yaw correctly turns head Look horizontally.
        assert(cog_raycast_head_local_offset(kind,forward,{{0,90,0}},out));
        assert(near(out[0],-4) && near(out[1],0) && near(out[2],0));
        // Roll changes right/up offsets, not the Look axis.
        assert(cog_raycast_head_local_offset(
            kind,{{1,0,0}},{{0,0,90}},out));
        assert(near(out[0],0) && near(out[1],0) && near(out[2],-1));
        // Composite PYR and non-cardinal offset preserves its total length.
        std::array<float,3> const offset{{2,3,4}};
        assert(cog_raycast_head_local_offset(kind,offset,{{25,40,-15}},out));
        float const len=std::sqrt(out[0]*out[0]+
                                  out[1]*out[1]+out[2]*out[2]);
        assert(near(len,std::sqrt(29.0f)));
    }
    // Invalid angles must not feed NaN directions into SithCollision.
    float const nan=std::numeric_limits<float>::quiet_NaN();
    assert(!cog_raycast_head_local_offset(10,forward,{{nan,0,0}},out));
    assert(!cog_raycast_head_local_offset(2,{{nan,0,0}},zero,out));

    // Combined orientation: body yaw 90 then head pitch 30.
    // Body Look points -X, so looking up also adds +world Z.
    jkgm::cog_pyr_basis body{};
    assert(jkgm::cog_pyr_build({{0,90,0}},body));
    assert(cog_raycast_head_local_offset(10,forward,{{30,0,0}},out));
    std::array<float,3> world{};
    for(int i=0;i<3;++i)
        world[i]=body.right[i]*out[0]+body.look[i]*out[1]+
                 body.up[i]*out[2];
    assert(near(world[0],-3.464102f) && near(world[1],0) &&
           near(world[2],2.0f));
}
