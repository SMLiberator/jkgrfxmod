#include "renderer/cog_thing_offsets.hpp"
#include <array>
#include <cassert>
#include <cmath>
#include <limits>
using namespace jkgm;
using vec=cog_thing_offset_vec;
using basis=cog_thing_offset_basis;
static bool close(float a,float b) { return std::fabs(a-b)<0.0002f; }
static void check(basis const &m,vec const &pos,vec const &global,
                  vec const &expected) {
    vec local{{99,99,99}};
    assert(cog_thing_pos_offset(m,pos,global,local));
    for(int k=0;k<3;++k)assert(close(local[k],expected[k]));
    vec world_again{{99,99,99}};
    assert(cog_thing_offset_pos(m,pos,local,world_again));
    for(int k=0;k<3;++k)assert(close(world_again[k],global[k]));
}
int main() {
    basis identity{{1,0,0, 0,1,0, 0,0,1}};
    check(identity,{{0,0,1}},{{0,4,0}},{{0,4,-1}});
    // User's example: +Look is world +X, Right is world -Y.
    basis oriented{{0,-1,0, 1,0,0, 0,0,1}};
    check(oriented,{{0,0,1}},{{0,4,0}},{{-4,0,-1}});
    check(oriented,{{4,5,6}},{{6,5,10}},{{0,2,4}});
    // +X local stretches 2x, +Y local stretches 3x, +Z 4x.
    basis scaled{{2,0,0, 0,3,0, 0,0,4}};
    check(scaled,{{1,2,3}},{{5,11,15}},{{2,3,3}});
    // Skew and a mirrored axis must remain exactly invertible.
    basis skew{{2,0,0, 1,3,0, 0,1,4}};
    check(skew,{{1,2,3}},{{7,8.5f,5}},{{2,2,0.5f}});
    basis mirror{{-1,0,0, 0,1,0, 0,0,1}};
    check(mirror,{{0,0,0}},{{4,3,2}},{{-4,3,2}});
    vec output{{9,9,9}};
    basis singular{{0,0,0, 0,1,0, 0,0,1}};
    assert(!cog_thing_pos_offset(singular,{{0,0,0}},{{0,1,0}},output));
    basis parallel{{1,0,0, 1,0,0, 0,0,1}};
    assert(!cog_thing_pos_offset(parallel,{{0,0,0}},{{1,0,0}},output));
    auto const nan=std::numeric_limits<float>::quiet_NaN();
    assert(!cog_thing_pos_offset(identity,{{0,0,nan}},{{1,1,1}},output));
    assert(!cog_thing_pos_offset(identity,{{0,0,0}},{{nan,1,1}},output));
    // Local->world is defined for a singular basis as well; it is a
    // direct affine map, unlike the inverse world->local query.
    vec world{};
    assert(cog_thing_offset_pos(singular,{{0,0,1}},{{7,4,3}},world));
    assert(close(world[0],0) && close(world[1],4) && close(world[2],4));
    assert(!cog_thing_offset_pos(identity,{{0,0,0}},{{nan,1,1}},world));
    assert(!cog_thing_offset_pos(identity,{{0,nan,0}},{{1,1,1}},world));
}
