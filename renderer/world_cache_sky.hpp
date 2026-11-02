#pragma once
// OpenJKDF2's SithWorld sky block (+0xDC..+0xF7), read-only.
// JKGM code addresses differ from OpenJKDF2; these DATA offsets are
// provisional pending inspection of the patched executable. Any bad
// parameters disable the corresponding sky mode rather than guessing.
#include "sprite_decal_world.hpp"
#include "cog_pyr_math.hpp"
#include "engine_camera_data.hpp"
#include <array>
#include <cmath>
#include <cstdint>

namespace jkgm {
constexpr uint32_t world_sky_horizon_flag=0x200U;
constexpr uint32_t world_sky_ceiling_flag=0x400U;
enum class world_cache_sky_kind:uint8_t {none=0,horizon=1,ceiling=2};
inline world_cache_sky_kind world_cache_sky_type(uint32_t flags) {
    if(flags&world_sky_horizon_flag)return world_cache_sky_kind::horizon;
    if(flags&world_sky_ceiling_flag)return world_cache_sky_kind::ceiling;
    return world_cache_sky_kind::none;
}
struct world_cache_sky_info {
    float ceiling_height=0.0f,horizon_distance=0.0f;
    float pixels_per_revolution=0.0f;
    std::array<float,2> horizon_offset{},ceiling_offset{};
    bool horizon_valid=false,ceiling_valid=false;
};
inline bool read_world_cache_sky_info(decal_native_read const &read,
                                     uintptr_t world,world_cache_sky_info &out)
{
    if(!read || !decal_native_address_ok(world) ||
       world>0x7ff00000U-0xf8U)return false;
    std::array<float,7> values{};
    if(!read(world+0xdcU,values.data(),sizeof(values)))return false;
    world_cache_sky_info info{};
    info.ceiling_height=values[0];
    info.horizon_distance=values[1];
    info.pixels_per_revolution=values[2];
    info.horizon_offset={{values[3],values[4]}};
    info.ceiling_offset={{values[5],values[6]}};
    auto valid_offset=[](std::array<float,2> const &v) {
        return std::isfinite(v[0]) && std::isfinite(v[1]) &&
            std::fabs(v[0])<1.e7f && std::fabs(v[1])<1.e7f;
    };
    info.horizon_valid=std::isfinite(info.horizon_distance) &&
        info.horizon_distance>0.0f && info.horizon_distance<100000.0f &&
        std::isfinite(info.pixels_per_revolution) &&
        info.pixels_per_revolution>0.0f &&
        info.pixels_per_revolution<1000000.0f &&
        valid_offset(info.horizon_offset);
    info.ceiling_valid=std::isfinite(info.ceiling_height) &&
        std::fabs(info.ceiling_height)<100000.0f &&
        valid_offset(info.ceiling_offset);
    if(!info.horizon_valid && !info.ceiling_valid)return false;
    out=info;return true;
}
inline bool world_cache_sky_angles(engine_camera const &camera,
                                  std::array<float,3> &out)
{
    if(!camera.valid)return false;
    cog_pyr_basis basis{};
    basis.right=camera.right;
    basis.look=camera.forward;
    basis.up=camera.up;
    auto pyr=cog_pyr_extract(basis);
    for(auto const angle:pyr)if(!std::isfinite(angle))return false;
    out=pyr;return true;
}
} // namespace jkgm
