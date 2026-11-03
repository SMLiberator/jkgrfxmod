#pragma once
// Verified against jkgm(5).exe:
// rdCache: 0x0043B48D initializes upload variant to 0;
// only 0x0043B717 selects variant 1 for unsupported hardware formats.
// rdMaterial_AddToTextureCache: 0x0044ACC0 dispatches that flag;
// std3D_AddToTextureCache: 0x0042A4F5/0x0042A50F selects 0x55C7FC
// in variant 1, which can be null. Never equate "opaque" with variant 1.
#include <cstdint>
namespace jkgm {
inline constexpr int native_mat_default_upload_variant=0;
inline constexpr uintptr_t native_mat_default_format_slot(
    bool texture_has_alpha,bool device_supports_alpha)
{
    return texture_has_alpha && device_supports_alpha
        ? 0x0055C7F8U : 0x0055C7F4U;
}
} // namespace jkgm
