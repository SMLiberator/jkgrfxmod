#pragma once
// JK's community high-resolution MAT extension keeps 3DO UVs in a
// per-axis pixel reference space capped at 256. Smaller native MATs
// continue to repeat at their actual dimensions.
//
// Example: UV span 0..256 on a 64px MAT repeats 4x; 128px repeats 2x;
// and 256px, 512px, or 2048px native MATs each span the whole texture.
#include <algorithm>
#include <cmath>

namespace jkgm {
inline float gpu_3do_model_uv_extent(float native_mip0_extent)
{
    if(!std::isfinite(native_mip0_extent) || native_mip0_extent<=0.0f)
        return 1.0f; // invalid MAT fallback, never divide by zero
    return std::min(native_mip0_extent,256.0f);
}
}
