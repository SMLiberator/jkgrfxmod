#pragma once
#include <algorithm>
#include <cmath>
#include <limits>

namespace editor_wheel {
// A vertical wheel tick changes an eligible material value by 0.05.
// Fractional deltas (e.g. precision touchpads) scale proportionally.
constexpr float tickStep=0.05f;

// Pure math helper: ImGui ownership/hover detection belongs to the caller.
inline bool adjust(float &value,float wheel,
                   float minimum=-std::numeric_limits<float>::max(),
                   float maximum=std::numeric_limits<float>::max()) {
    if(wheel==0.0f||!std::isfinite(wheel)||!std::isfinite(value)||
       !std::isfinite(minimum)||!std::isfinite(maximum)||
       minimum>maximum)
        return false;
    const auto updated=static_cast<float>(std::clamp(
        double(value)+double(wheel)*double(tickStep),
        double(minimum),double(maximum)));
    if(updated==value)return false;
    value=updated;
    return true;
}
} // namespace editor_wheel
