#pragma once
// JK/Sith uses type=1 for Camera Things and rdThing.type=1 for 3DOs.
// Do not conflate the Sith Thing type with the active rdCamera or with
// ThingFlag 0x20 (skybox/light exemption, still post-processed).
#include <cstdint>
namespace jkgm {
constexpr uint32_t camera_hud_sith_thing_type=1U;
constexpr uint32_t camera_hud_rd_model_type=1U;
inline bool is_camera_hud_model(uint32_t thing_type,uint32_t rd_type)
{
    return thing_type==camera_hud_sith_thing_type &&
           rd_type==camera_hud_rd_model_type;
}
}
