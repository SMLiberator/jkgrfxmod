#pragma once

// Preview-side equivalents of the renderer's independent material animation
// clock. Kept free of OpenGL/ImGui so the math can be regression-tested.
#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <limits>

namespace editor_animation {
enum class Wave { Sine, Square, Flicker, Random };
struct Pulse {
    bool enabled=false;
    Wave wave=Wave::Sine;
    float rate=1.0f; // seconds per cycle, NOT cycles per second
    float min=0.0f;
    float max=1.0f;
    float phase=0.0f;
};

// Match renderer::scroll_offset: reduce in double precision before sending
// UV offsets to GLSL, retaining negative scroll directions.
inline std::array<float,2> scrollOffset(const std::array<float,2> &speed,
                                        double seconds) {
    return {static_cast<float>(std::fmod(double(speed[0])*seconds,1.0)),
            static_cast<float>(std::fmod(double(speed[1])*seconds,1.0))};
}

// Same stateless hash as renderer::pulse_random. Instance IDs in the editor
// are stable material-list indices, not game-time material instance IDs.
inline float pulseRandom(std::uint64_t materialId,std::int64_t step) {
    std::uint64_t x=materialId*0x9E3779B97F4A7C15ULL+
                    std::uint64_t(step)*0xD1B54A32D192ED03ULL;
    x^=x>>30;x*=0xBF58476D1CE4E5B9ULL;
    x^=x>>27;x*=0x94D049BB133111EBULL;
    x^=x>>31;
    return float(x>>40)/16777216.0f;
}

inline float multiplier(const Pulse &pulse,double seconds,
                        std::uint64_t materialId) {
    if(!pulse.enabled||!(pulse.rate>0.0f))return 1.0f;
    const double cycles=seconds/double(pulse.rate)+double(pulse.phase);
    if(!std::isfinite(cycles))return 1.0f;
    const double floored=std::floor(cycles);
    // Guard unusually large preview times against undefined float-to-int.
    if(floored<double(std::numeric_limits<std::int64_t>::min())||
       floored>=double(std::numeric_limits<std::int64_t>::max()))
        return 1.0f;
    const auto step=static_cast<std::int64_t>(floored);
    const float fraction=static_cast<float>(cycles-floored);
    float level=1.0f;
    switch(pulse.wave) {
    case Wave::Sine:
        level=0.5f-0.5f*std::cos(fraction*6.283185307179586f);
        break;
    case Wave::Square:
        level=fraction<0.5f?1.0f:0.0f;
        break;
    case Wave::Flicker:
        level=pulseRandom(materialId,step)<0.5f?0.0f:1.0f;
        break;
    case Wave::Random: {
        const float a=pulseRandom(materialId,step);
        const float b=pulseRandom(materialId,step+1);
        const float smooth=fraction*fraction*(3.0f-2.0f*fraction);
        level=a+(b-a)*smooth;
        break;
    }
    }
    return pulse.min+(pulse.max-pulse.min)*level;
}
} // namespace editor_animation
