#pragma once
// Bound authored JK lighting inputs without clipping their sum or added lights.
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
} // namespace jkgm
