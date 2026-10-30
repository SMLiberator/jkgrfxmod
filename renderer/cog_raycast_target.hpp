#pragma once
#include <cstdint>

namespace jkgm {
// The native Sith collision table permits Item <-> Player only. Corpse has
// no Thing-Thing collision handlers (world surfaces still collide normally).
// Explicit COG rays must preserve these rules even if a search implementation
// returns an otherwise valid intersection with such a candidate.
//
// The source Thing's type is used; this does not prohibit player pickup rays.
// Only apply to COG RayCast/RayCastEx; never to normal gameplay collision.
inline bool cog_raycast_eligible_thing_pair(uint32_t source_type,
                                             uint32_t target_type)
{
    constexpr uint32_t item=5U,corpse=9U,player=10U;
    if(source_type==corpse || target_type==corpse)return false;
    if(source_type==item && target_type!=player)return false;
    if(target_type==item && source_type!=player)return false;
    return true; // Remaining combinations follow JK's native handler table.
}
} // namespace jkgm
