#pragma once
// JK/Sith uses type=1 for Camera Things and rdThing.type=1 for 3DOs.
// Do not conflate the Sith Thing type with the active rdCamera.
// ThingFlag 0x20 retains its skybox/light-exemption meaning on ordinary
// Things; on Camera Things it also requests occlusion by opaque world depth.
#include <cstdint>
#include "sun_skybox_thing.hpp"
namespace jkgm {
constexpr uint32_t camera_hud_sith_thing_type=1U;
constexpr uint32_t camera_hud_rd_model_type=1U;
inline bool is_camera_hud_model(uint32_t thing_type,uint32_t rd_type)
{
    return thing_type==camera_hud_sith_thing_type &&
           rd_type==camera_hud_rd_model_type;
}
// Only Camera Things use 0x20 to select depth-tested GUI. Ordinary skyboxes
// remain scene objects; the flag never moves them into the late HUD pass.
inline bool camera_hud_uses_world_depth(bool is_camera_thing,uint32_t flags)
{
    return is_camera_thing && (flags&thingflag_skybox)!=0U;
}
}
