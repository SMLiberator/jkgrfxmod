#pragma once

// Experimental exact 8-bit input -> gamma 1.8 conversion fast path.
// JK's diffuse RGBA has 256 possible values per channel; the alpha
// premultiplication happens BEFORE gamma 1.8 conversion. A 256x256
// lookup table preserves this ordering and prevents nine powf calls per
// submitted triangle. One 256 KiB table is shared across all vertices.
#include "math/color.hpp"
#include <array>
#include <cmath>
#include <cstdint>

namespace jkgm {
    inline color gamma18_premultiplied_rgba8_lut(uint8_t red,
                                                 uint8_t green,
                                                 uint8_t blue,
                                                 uint8_t alpha)
    {
        static const auto channel_lut = [] {
            std::array<float, 256u * 256u> result{};
            for(unsigned c = 0; c < 256; ++c) {
                float const ch = float(c) / 255.0f;
                for(unsigned a = 0; a < 256; ++a) {
                    float const af = float(a) / 255.0f;
                    result[c * 256u + a] = std::pow(ch * af, 1.8f);
                }
            }
            return result;
        }();
        return color(channel_lut[(unsigned(red) << 8) | alpha],
                     channel_lut[(unsigned(green) << 8) | alpha],
                     channel_lut[(unsigned(blue) << 8) | alpha],
                     float(alpha) / 255.0f);
    }
}
