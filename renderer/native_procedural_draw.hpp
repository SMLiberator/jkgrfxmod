#pragma once
// Verified against user-supplied jkgm(5).exe SHA-256:
// afb4605c8259075c151ad6b034b98562be87ebe90369d4d9abaee09b396ed50f.
// JKGM's rdThing_Draw and procedural drawing addresses differ from OpenJKDF2.
// Read-only definitions. Never hook a procedure without verifying its bytes.
#include <array>
#include <cstddef>
#include <cstdint>
#include <cstring>
namespace jkgm {
namespace native_procedural_draw {
constexpr uintptr_t rdthing_draw=0x0043E790U;
constexpr uintptr_t rdpolyline_draw=0x00448D60U;
constexpr uintptr_t rdpolyline_draw_face=0x004491F0U;
constexpr uintptr_t rdparticle_draw=0x0046C9A0U;
constexpr uintptr_t rdcache_add_proc_face=0x0043B240U;
constexpr uintptr_t rdthing_polyline_ptr_offset=0x04U;
constexpr uintptr_t rdthing_particle_ptr_offset=0x04U;
constexpr uintptr_t polyline_length_offset=0x20U;
constexpr uintptr_t polyline_base_radius_offset=0x24U;
constexpr uintptr_t polyline_tip_radius_offset=0x28U;
constexpr uintptr_t polyline_side_face_offset=0x38U;
constexpr uintptr_t polyline_tip_face_offset=0x78U;
constexpr uintptr_t polyline_side_uv_offset=0xB8U;
constexpr uintptr_t polyline_tip_uv_offset=0xBCU;
// rdFace.material is +0x20 relative to either embedded face.
constexpr uintptr_t polyline_face_material_offset=0x20U;
// rdFace.texVertOffset is the two-float field following rdFace.wallCel.
constexpr uintptr_t polyline_face_uv_offset_offset=0x28U;
constexpr uintptr_t particle_count_offset=0x24U;
constexpr uintptr_t particle_positions_offset=0x28U;
constexpr uintptr_t particle_cels_offset=0x2CU;
constexpr uintptr_t particle_size_offset=0x34U;
constexpr uintptr_t particle_material_offset=0x38U;
constexpr uintptr_t particle_light_mode_offset=0x40U;

// Function prologues and the exact dispatcher's polyline CALL.
constexpr std::array<uint8_t,16> polyline_signature{{
    0x81,0xEC,0x90,0x00,0x00,0x00,0x53,0x56,
    0x57,0x8B,0x94,0x24,0xA0,0x00,0x00,0x00}};
constexpr std::array<uint8_t,16> polyline_face_signature{{
    0x83,0xEC,0x20,0x53,0x55,0x56,0x57,0xE8,
    0x94,0x1E,0xFF,0xFF,0x8B,0xF0,0x85,0xF6}};
constexpr std::array<uint8_t,16> particle_signature{{
    0xA1,0x18,0xA4,0x73,0x00,0x83,0xEC,0x44,
    0x83,0xC0,0x08,0x8D,0x54,0x24,0x08,0x53}};
constexpr std::array<uint8_t,11> polyline_dispatch_signature{{
    0x8B,0x54,0x24,0x08,0x52,0x50,0xE8,0x77,
    0xA5,0x00,0x00}};

template <typename Reader,size_t N>
inline bool verify_bytes(Reader const &read,uintptr_t address,
                         std::array<uint8_t,N> const &expected)
{
    std::array<uint8_t,N> actual{};
    return read(address,actual.data(),actual.size()) &&
           std::memcmp(actual.data(),expected.data(),N)==0;
}
template <typename Reader>
inline bool verified(Reader const &read)
{
    return verify_bytes(read,rdpolyline_draw,polyline_signature) &&
           verify_bytes(read,rdpolyline_draw_face,polyline_face_signature) &&
           verify_bytes(read,rdparticle_draw,particle_signature) &&
           verify_bytes(read,rdthing_draw+0x4EU,polyline_dispatch_signature);
}
} // namespace native_procedural_draw
} // namespace jkgm
