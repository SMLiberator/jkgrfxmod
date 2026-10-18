#pragma once

#include "base/log.hpp"
#include "engine_camera_data.hpp"
#include "shaders.hpp"
#include "sun.hpp"
#include <Windows.h>
#include <cmath>
#include <cstdint>
#include <cstring>

namespace jkgm {
    // Reads the engine's current camera straight out of jk.exe's memory (no hooks), so effects
    // can follow the real view: its orientation, and its field of view, which COG scripts change
    // at runtime (zoom, speed effects, ...).
    //
    // These addresses belong to one specific build of jk.exe (the one the community patch's
    // JK.dll was written for; JK.dll uses the same camera array and current-camera index).
    // Before reading anything the start of two engine functions is compared with what is
    // expected, and every value read is sanity-checked; if anything is off the camera counts
    // as unavailable and the shaders fall back to their fixed-FOV, camera-attached behaviour.
    namespace engine_camera_detail {
        constexpr uintptr_t camera_array_address = 0x8EC2C0;
        constexpr uintptr_t current_camera_index_address = 0x8EE4E8;
        constexpr size_t camera_size = 0x4E0;
        constexpr int camera_count = 7;

        struct code_signature {
            uintptr_t address;
            std::array<uint8_t, 8> bytes;
        };

        // rdCamera_SetFOV and rdCamera_SetAspectRatio
        constexpr std::array<code_signature, 2> signatures = {{
            {0x443720, {0xd9, 0x44, 0x24, 0x08, 0xd8, 0x1d, 0x84, 0x2c}},
            {0x443810, {0x8b, 0x4c, 0x24, 0x04, 0xd9, 0x44, 0x24, 0x08}},
        }};

        inline bool readable(uintptr_t address, size_t size)
        {
            MEMORY_BASIC_INFORMATION mbi{};
            if(VirtualQuery(reinterpret_cast<void const *>(address), &mbi, sizeof(mbi)) == 0) {
                return false;
            }

            if(mbi.State != MEM_COMMIT || (mbi.Protect & (PAGE_NOACCESS | PAGE_GUARD)) != 0) {
                return false;
            }

            auto const region_end = reinterpret_cast<uintptr_t>(mbi.BaseAddress) + mbi.RegionSize;
            return address + size <= region_end;
        }

        inline bool build_matches()
        {
            static bool const matches = [] {
                for(auto const &sig : signatures) {
                    if(!readable(sig.address, sig.bytes.size()) ||
                       std::memcmp(reinterpret_cast<void const *>(sig.address),
                                   sig.bytes.data(),
                                   sig.bytes.size()) != 0) {
                        LOG_DEBUG("jk.exe is not the expected build; reflections will use the "
                                  "fixed field of view and a camera-attached environment");
                        return false;
                    }
                }

                return true;
            }();

            return matches;
        }
    }

    inline engine_camera read_engine_camera()
    {
        using namespace engine_camera_detail;

        // Log only when the outcome changes, not every frame
        static int last_state = -1;
        auto report = [&](int state, char const *message) {
            if(state != last_state) {
                last_state = state;
                LOG_DEBUG(message);
            }
        };

        if(!build_matches()) {
            return engine_camera{};
        }

        if(!readable(current_camera_index_address, sizeof(int))) {
            report(1, "Engine camera index is not readable");
            return engine_camera{};
        }

        int index = 0;
        std::memcpy(&index,
                    reinterpret_cast<void const *>(current_camera_index_address),
                    sizeof(index));
        if(index < 0 || index >= camera_count) {
            report(2, "Engine camera index is out of range");
            return engine_camera{};
        }

        uintptr_t const camera_address = camera_array_address + (size_t)index * camera_size;
        if(!readable(camera_address, engine_camera_layout::read_size)) {
            report(3, "Engine camera is not readable");
            return engine_camera{};
        }

        uint8_t raw[engine_camera_layout::read_size];
        std::memcpy(raw, reinterpret_cast<void const *>(camera_address), sizeof(raw));

        engine_camera_status status = engine_camera_status::ok;
        auto cam = parse_engine_camera(raw, &status);
        if(!cam.valid) {
            report(4,
                   status == engine_camera_status::bad_fov
                       ? "Engine camera field of view is not plausible"
                       : "Engine camera orientation is not a valid rotation");
            return cam;
        }

        report(0, "Reading the engine camera for reflections");
        return cam;
    }

    // Hands the engine camera to a game pass shader. Call once per pass, before drawing.
    inline void apply_engine_camera(shaders::game_pass_shader &shader)
    {
        auto const cam = read_engine_camera();
        auto const &sun = get_sun_settings();
        if(!cam.valid) {
            // Without camera orientation we cannot apply a world-space sun.
            shader.set_sun(make_point(0.0f, 0.0f, 1.0f, 0.0f),
                           make_point(0.0f, 0.0f, 0.0f, 0.0f));
            // w = 0 tells the shader that no camera is available
            shader.set_camera(make_point(1.0f, 0.0f, 0.0f, 0.0f),
                              make_point(0.0f, 1.0f, 0.0f, 0.0f),
                              make_point(0.0f, 0.0f, 1.0f, 0.0f),
                              make_point(1.0f, 0.0f, 0.0f, 0.0f));
            return;
        }

        // The engine's FOV is horizontal, so tan(fov / 2) is how far to the screen's side edge
        // a view ray reaches per unit of depth
        float const half_fov = cam.fov * 0.5f * 3.14159265358979f / 180.0f;

        // Convert world X/Y/Z to view right/up/forward using camera axes.
        auto const dir = normalized_sun_direction(sun.direction.value());
        auto const project = [&](std::array<float, 3> const &axis) {
            return dir[0]*axis[0] + dir[1]*axis[1] + dir[2]*axis[2];
        };
        auto const rgb = sun.color.value();
        shader.set_sun(make_point(project(cam.right), project(cam.up),
                                  project(cam.forward), 1.0f),
                       make_point(rgb[0], rgb[1], rgb[2],
                                  sun.intensity.value()[0]));

        shader.set_camera(make_point(cam.right[0], cam.right[1], cam.right[2], 1.0f),
                          make_point(cam.forward[0], cam.forward[1], cam.forward[2], 1.0f),
                          make_point(cam.up[0], cam.up[1], cam.up[2], 1.0f),
                          make_point(std::tan(half_fov), 0.0f, 0.0f, 0.0f));
    }
}
