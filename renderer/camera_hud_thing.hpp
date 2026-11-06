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
// Overlay HUDs have no meaningful painter-depth order: they are authored as
// a sequence of overlaid meshes. Sorting their nearly coplanar centroids
// every frame reverses blended layers as the camera moves, causing flicker.
// Only 0x20 world-depth HUDs may request back-to-front sorting; even there
// sort whole mesh instances at their common origin, not individual faces.
// Draw world-depth UI first so it never covers the overlay HUD layer.
inline bool camera_hud_draw_precedes(bool a_world_depth,float a_depth,
                                    unsigned long long a_order,
                                    bool b_world_depth,float b_depth,
                                    unsigned long long b_order,
                                    bool sort_world_depth)
{
    if(a_world_depth!=b_world_depth)return a_world_depth;
    if(a_world_depth && sort_world_depth && a_depth!=b_depth)
        return a_depth>b_depth;
    return a_order<b_order;
}
}
