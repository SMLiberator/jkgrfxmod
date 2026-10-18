#pragma once

// Read-only diagnostic for the original JK 2.3 executable's MAT-to-D3D cache link.
// Addresses and offsets were checked against jk23_rgb_smoothnormals_fixed_v2.exe.
// No engine hooks or modifications are installed.

#include "engine_camera.hpp"
#include <Windows.h>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <string>
#include <vector>

namespace jkgm {
namespace engine_material_debug {

constexpr uintptr_t current_world_global = 0x833A10;
constexpr uintptr_t static_world_global = 0x833A14;

#pragma pack(push, 4)
struct world_header {
    uint32_t level_type;
    char level_name[32];
    char episode_name[32];
    uint32_t num_colormaps;
    uint32_t colormaps;
    uint32_t num_sectors;
    uint32_t sectors;
    int32_t num_materials;                 // 0x54
    int32_t capacity_materials;            // 0x58
    uint32_t materials;                    // 0x5C
};
struct native_material {
    uint32_t tex_type;
    char name[32];
    uint32_t id;
    uint32_t format[14];                   // JK rdTexFormat, 56 bytes
    uint32_t palette;
    uint32_t num_texinfo;
    uint32_t current_cel;
    uint32_t texinfo[16];
    uint32_t num_textures;
    uint32_t textures;
};
struct native_texinfo {
    uint8_t header[28];
    uint32_t texture;
};
struct native_surface {
    uint32_t ddraw_vtable;
    uint32_t d3d_texture;
    uint8_t surface_desc[0x6c];
    int32_t texture_handle;
    int32_t texture_loaded;
    uint32_t is_16bit;
    uint32_t width;
    uint32_t height;
    uint32_t size;
    uint32_t frame;
    uint32_t cache_prev;
    uint32_t cache_next;
};
struct native_texture {
    uint32_t header[7];
    uint32_t mipmap_buffers[4];
    native_surface alpha_mips[4];
    native_surface opaque_mips[4];
};
#pragma pack(pop)

static_assert(sizeof(world_header) == 0x60, "World material table offsets changed");
static_assert(offsetof(world_header, num_materials) == 0x54, "Wrong material count offset");
static_assert(offsetof(world_header, materials) == 0x5c, "Wrong material array offset");
static_assert(sizeof(native_material) == 0xb4, "Wrong JK rdMaterial stride");
static_assert(offsetof(native_material, id) == 0x24, "Wrong JK MAT ID offset");
static_assert(offsetof(native_material, texinfo) == 0x6c, "Wrong texinfo array offset");
static_assert(offsetof(native_material, textures) == 0xb0, "Wrong JK texture array offset");
static_assert(sizeof(native_texinfo) == 0x20, "Wrong rdTexinfo stride");
static_assert(sizeof(native_surface) == 0x98, "Wrong D3D surface stride");
static_assert(offsetof(native_surface, texture_handle) == 0x74, "Wrong D3D handle offset");
static_assert(sizeof(native_texture) == 0x4ec, "Wrong rdTexture stride");

struct texture_link {
    bool is_static = false;
    uint32_t mat_id = 0;
    uint32_t cel = 0;
    uint32_t mip = 0;
    bool alpha = false;
    uint32_t handle = 0;
    std::string name;
};

struct snapshot {
    bool supported_build = false;
    uint32_t current_world = 0;
    uint32_t static_world = 0;
    uint32_t current_materials = 0;
    uint32_t static_materials = 0;
    uint32_t current_count = 0;
    uint32_t static_count = 0;
    std::string level_name;
    size_t visited_materials = 0;
    size_t rejected_materials = 0;
    std::vector<texture_link> textures;
};

inline bool read_bytes(uintptr_t addr, void *dest, size_t len)
{
    if(addr < 0x10000 || addr > 0x7fffffff || len > 0x100000 ||
       !engine_camera_detail::readable(addr, len)) {
        return false;
    }
    std::memcpy(dest, reinterpret_cast<void const *>(addr), len);
    return true;
}

template <typename T>
inline bool read_object(uintptr_t addr, T &out)
{
    return read_bytes(addr, &out, sizeof(out));
}

inline bool has_signature(uintptr_t address, uint8_t const *signature, size_t len)
{
    uint8_t found[16]{};
    return len <= sizeof(found) && read_bytes(address, found, len) &&
           std::memcmp(found, signature, len) == 0;
}

inline bool matches_tested_executable()
{
    static bool const matches = [] {
        // Independent of the camera check: verifies the original COG
        // PopMaterial lookup's actual current/static global addresses and
        // material count/array accesses in this JK executable.
        constexpr uint8_t entry[] = {0x55, 0x8b, 0xec, 0x83, 0xec, 0x10};
        constexpr uint8_t current[] = {0x8b, 0x3d, 0x10, 0x3a, 0x83, 0x00};
        constexpr uint8_t shared[] = {0x8b, 0x3d, 0x14, 0x3a, 0x83, 0x00};
        constexpr uint8_t count[] = {0x3b, 0x47, 0x54};
        constexpr uint8_t array[] = {0x8b, 0x4f, 0x5c};
        return engine_camera_detail::build_matches() &&
               has_signature(0x4E3040, entry, sizeof(entry)) &&
               has_signature(0x4E3052, current, sizeof(current)) &&
               has_signature(0x4E3104, shared, sizeof(shared)) &&
               has_signature(0x4E3115, count, sizeof(count)) &&
               has_signature(0x4E311A, array, sizeof(array));
    }();
    return matches;
}

inline std::string printable_name(char const *buf, size_t capacity)
{
    size_t n = 0;
    while(n < capacity && buf[n]) {
        unsigned char c = static_cast<unsigned char>(buf[n]);
        if(c < 32 || c > 126) {
            return "<invalid>";
        }
        ++n;
    }
    return n == 0 ? "<unnamed>" : std::string(buf, n);
}

inline bool load_world(uintptr_t global, uint32_t &address, world_header &world)
{
    if(!read_object(global, address) || !address || !read_object(address, world)) {
        return false;
    }
    return world.num_materials >= 0 && world.num_materials <= 16384 &&
           world.capacity_materials >= world.num_materials &&
           world.capacity_materials <= 16384 && world.materials != 0;
}

inline void scan_world(snapshot &out, bool shared, world_header const &world)
{
    for(int32_t index = 0; index < world.num_materials; ++index) {
        native_material mat{};
        uintptr_t const address =
            static_cast<uintptr_t>(world.materials) + size_t(index) * sizeof(mat);
        if(!read_object(address, mat)) {
            ++out.rejected_materials;
            continue;
        }

        uint32_t const mat_id = uint32_t(index) | (shared ? 0x8000U : 0U);
        if(mat.id != mat_id || mat.num_texinfo > 16U ||
           mat.num_textures > 256U || mat.num_textures == 0U ||
           mat.textures == 0U) {
            ++out.rejected_materials;
            continue;
        }
        ++out.visited_materials;
        std::string const name = printable_name(mat.name, sizeof(mat.name));

        // A MAT cel indexes a texinfo. Different cels may point at the same
        // texture, so don't assume cel == index in mat.textures.
        for(uint32_t cel = 0; cel < mat.num_texinfo; ++cel) {
            if(!mat.texinfo[cel]) {
                continue;
            }
            native_texinfo info{};
            if(!read_object(mat.texinfo[cel], info) || !info.texture ||
               info.texture < mat.textures) {
                continue;
            }
            uint64_t const byte_offset = uint64_t(info.texture) - mat.textures;
            if(byte_offset % sizeof(native_texture) != 0 ||
               byte_offset / sizeof(native_texture) >= mat.num_textures) {
                continue;
            }
            native_texture texture{};
            if(!read_object(info.texture, texture)) {
                continue;
            }
            uint32_t const mip_count = texture.header[6];
            if(mip_count == 0 || mip_count > 4) {
                continue;
            }
            for(uint32_t mip = 0; mip < mip_count; ++mip) {
                for(unsigned alpha = 0; alpha < 2; ++alpha) {
                    auto const &surface =
                        alpha ? texture.alpha_mips[mip] : texture.opaque_mips[mip];
                    if(surface.texture_loaded == 0 || surface.texture_handle <= 0) {
                        continue;
                    }
                    out.textures.push_back(texture_link{
                        shared, mat_id, cel, mip, alpha != 0,
                        uint32_t(surface.texture_handle), name});
                }
            }
        }
    }
}

inline snapshot scan()
{
    snapshot out;
    if(!matches_tested_executable()) {
        return out; // Refuse to read arbitrary engine state on unknown builds.
    }
    out.supported_build = true;
    world_header world{};
    if(load_world(current_world_global, out.current_world, world)) {
        out.current_materials = world.materials;
        out.current_count = uint32_t(world.num_materials);
        out.level_name = printable_name(world.level_name, sizeof(world.level_name));
        scan_world(out, false, world);
    }
    if(load_world(static_world_global, out.static_world, world)) {
        out.static_materials = world.materials;
        out.static_count = uint32_t(world.num_materials);
        scan_world(out, true, world);
    }
    return out;
}

} // namespace engine_material_debug
} // namespace jkgm
