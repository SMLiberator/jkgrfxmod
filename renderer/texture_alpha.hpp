#pragma once
#include "base/span.hpp"
#include <cstddef>
namespace jkgm {
    // Check RGBA8 once when a texture is uploaded, including source PNGs.
    // Even binary 0/255 alpha matters: bilinear filtering makes soft edges.
    inline bool texture_has_nonopaque_alpha(span<char const> rgba)
    {
        for(size_t i=3;i<rgba.size();i+=4)
            if(static_cast<unsigned char>(rgba[i])!=255U) return true;
        return false;
    }
}
