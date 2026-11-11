#pragma once
// Cap authored and completed JK light consistently with the selected range.
#include <algorithm>

namespace jkgm {
inline constexpr float authored_light_limit(bool overbright) {
    return overbright ? 2.0f : 1.0f;
}
// Baked vertex intensity and ambient are nonnegative light floors.
inline float clamp_authored_light(float value,bool overbright) {
    return std::clamp(value,0.0f,authored_light_limit(overbright));
}
// Negative ExtraLight is an authored darkening contribution: retain it.
inline float clamp_authored_extra_light(float value,bool overbright) {
    return std::min(value,authored_light_limit(overbright));
}
// Face ExtraLight is additive, but its sum with the static vertex light
// is still bounded. Negative face ExtraLight retains its darkening behavior.
inline float clamp_authored_baked_sum(float vertex,float face_extra,
                                     bool overbright) {
    return std::clamp(clamp_authored_light(vertex,overbright)+
                      clamp_authored_extra_light(face_extra,overbright),
                      0.0f,authored_light_limit(overbright));
}

// SithSector.extraLight and sithRender global ExtraLight form ONE floor.
inline float clamp_authored_extra_floor(float sector_extra,
                                       float global_extra,bool overbright) {
    return std::clamp(clamp_authored_extra_light(sector_extra,overbright)+
                      clamp_authored_extra_light(global_extra,overbright),
                      0.0f,authored_light_limit(overbright));
}

// Native JK caps the completed world Gouraud light at 1. Overbright keeps
// the authored cap at 2 but permits additional dynamic HDR contributions.
inline float compose_authored_world_brightness(float raw,float dynamic,
                                               float sector_floor,bool overbright) {
    float const lit=std::max(std::max(raw+dynamic,0.0f),sector_floor);
    return overbright?lit:std::min(lit,1.0f);
}
} // namespace jkgm
