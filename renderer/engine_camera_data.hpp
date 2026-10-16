#pragma once

#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstring>

namespace jkgm {
    // The engine's current camera, as read from jk.exe's memory.
    struct engine_camera {
        bool valid = false;

        // The camera's axes as directions in world space (JK's world has x and y horizontal, z up)
        std::array<float, 3> right{1.0f, 0.0f, 0.0f};
        std::array<float, 3> forward{0.0f, 1.0f, 0.0f};
        std::array<float, 3> up{0.0f, 0.0f, 1.0f};

        // The camera's position in world space
        std::array<float, 3> position{0.0f, 0.0f, 0.0f};

        // Horizontal field of view in degrees, as used for the projection
        float fov = 90.0f;
    };

    enum class engine_camera_status {
        ok,
        bad_matrix,
        bad_fov,
    };

    // Layout of one SithCamera in jk.exe (the same structure the community patch's JK.dll uses:
    // an array of 7, 0x4E0 bytes apart). Only the first 0xC0 bytes are needed.
    namespace engine_camera_layout {
        constexpr size_t read_size = 0xC0;

        // SithCamera.orient: camera-to-world rdMatrix34 { rvec, lvec, uvec, position },
        // each a vector of three floats. rvec = right, lvec = forward, uvec = up.
        constexpr size_t orient_offset = 0x34;

        // SithCamera.rdCamera starts at 0x7C; its fov (degrees) is 0x38 into it. This is the value
        // rdCamera_SetFOV stores and the projection is built from.
        constexpr size_t fov_offset = 0x7C + 0x38;
    }

    namespace engine_camera_detail {
        inline float read_float(uint8_t const *raw, size_t offset)
        {
            float value;
            std::memcpy(&value, raw + offset, sizeof(value));
            return value;
        }

        inline std::array<float, 3> read_vec3(uint8_t const *raw, size_t offset)
        {
            return {read_float(raw, offset),
                    read_float(raw, offset + 4),
                    read_float(raw, offset + 8)};
        }

        inline float dot(std::array<float, 3> const &a, std::array<float, 3> const &b)
        {
            return a[0] * b[0] + a[1] * b[1] + a[2] * b[2];
        }

        inline bool finite(std::array<float, 3> const &v)
        {
            return std::isfinite(v[0]) && std::isfinite(v[1]) && std::isfinite(v[2]);
        }
    }

    // Interprets the first engine_camera_layout::read_size bytes of a SithCamera. The checks
    // double as a test that the layout really matches this build of jk.exe: the three axes must
    // be unit length and at right angles, and the FOV must be one the engine allows (5 to 179).
    inline engine_camera parse_engine_camera(uint8_t const *raw,
                                             engine_camera_status *status = nullptr)
    {
        using namespace engine_camera_detail;

        auto fail = [&](engine_camera_status s) {
            if(status != nullptr) {
                *status = s;
            }

            return engine_camera{};
        };

        engine_camera cam;
        cam.right = read_vec3(raw, engine_camera_layout::orient_offset);
        cam.forward = read_vec3(raw, engine_camera_layout::orient_offset + 12);
        cam.up = read_vec3(raw, engine_camera_layout::orient_offset + 24);
        cam.position = read_vec3(raw, engine_camera_layout::orient_offset + 36);
        cam.fov = read_float(raw, engine_camera_layout::fov_offset);

        if(!finite(cam.right) || !finite(cam.forward) || !finite(cam.up) || !finite(cam.position)) {
            return fail(engine_camera_status::bad_matrix);
        }

        for(auto const *axis : {&cam.right, &cam.forward, &cam.up}) {
            float const len = std::sqrt(dot(*axis, *axis));
            if(!(len > 0.98f && len < 1.02f)) {
                return fail(engine_camera_status::bad_matrix);
            }
        }

        if(std::fabs(dot(cam.right, cam.forward)) > 0.02f ||
           std::fabs(dot(cam.right, cam.up)) > 0.02f ||
           std::fabs(dot(cam.forward, cam.up)) > 0.02f) {
            return fail(engine_camera_status::bad_matrix);
        }

        if(!(cam.fov >= 5.0f && cam.fov <= 179.0f)) {
            return fail(engine_camera_status::bad_fov);
        }

        cam.valid = true;
        if(status != nullptr) {
            *status = engine_camera_status::ok;
        }

        return cam;
    }
}
