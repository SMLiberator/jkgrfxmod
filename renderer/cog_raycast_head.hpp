#pragma once
// Sith's actor/player headPYR rotates their aiming frame *relative* to the
// Thing's body orientation (rdMatrix_PreRotate34). RayCast/RayCastEx retain the
// source Thing's origin and use this adjusted frame for the local offset.
#include "cog_pyr_math.hpp"
#include <array>
#include <cmath>
#include <cstdint>

namespace jkgm {
// Native JK SithThing types: ACTOR=2, PLAYER=10.
// Other Thing types do not have valid actorParams.headPYR.
inline bool cog_raycast_head_local_offset(uint32_t thing_type,
                                          std::array<float,3> const& offset,
                                          std::array<float,3> const& head_pyr,
                                          std::array<float,3>& result)
{
    for(float v:offset)if(!std::isfinite(v))return false;
    result=offset;
    if(thing_type!=2U && thing_type!=10U)return true;
    for(float angle:head_pyr)if(!std::isfinite(angle))return false;
    if(head_pyr[0]==0.0f && head_pyr[1]==0.0f &&
       head_pyr[2]==0.0f)return true;
    cog_pyr_basis head{};
    if(!cog_pyr_build(head_pyr,head))return false;
    for(int i=0;i<3;++i)
        result[i]=head.right[i]*offset[0]+
                  head.look[i]*offset[1]+
                  head.up[i]*offset[2];
    for(float v:result)if(!std::isfinite(v))return false;
    return true;
}
} // namespace jkgm
