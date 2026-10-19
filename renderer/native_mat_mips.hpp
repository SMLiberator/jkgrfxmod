#pragma once
// Select the highest-resolution image in a native Sith MAT.
// OpenGL still generates and selects its own GPU mipmaps.
namespace jkgm {
    constexpr float native_mat_full_resolution_distance=1.0e30f;
    inline void prefer_native_mat_mip_zero(float *thresholds)
    {
        for(int i=0;i<4;i++)
            thresholds[i]=native_mat_full_resolution_distance;
    }
}
