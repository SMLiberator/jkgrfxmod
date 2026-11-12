#pragma once
#include <algorithm>
#include <cmath>

namespace jkgm {
    // Pure per-frame PCF uniform calculation. No new shadow-map samples.
    // This is an artistic sun-intensity response, not PCSS blocker search.
    struct adaptive_sun_pcf_radii {
        float near_radius=0.0f;
        float far_radius=0.0f;
    };

    inline adaptive_sun_pcf_radii sun_intensity_pcf_radii(
        float near_base,float far_base,float intensity,
        float max_softening,float reference)
    {
        if(!(max_softening>0.0f) || !(reference>0.0f) ||
           !std::isfinite(intensity) || !std::isfinite(max_softening) ||
           !std::isfinite(reference))
            return {near_base,far_base};
        // Dimmer daylight suggests a softer, more diffuse apparent source.
        // No physical blocker/receiver separation can be inferred here.
        float const relative=std::clamp(intensity/reference,0.0f,1.0f);
        float const factor=1.0f+max_softening*(1.0f-relative);
        auto radius=[&](float base)->float {
            // Preserve 1-tap PCF for configured 0-radius paths.
            if(!(base>0.0f))return base;
            return std::min(4.0f,base*factor);
        };
        return {radius(near_base),radius(far_base)};
    }
}
