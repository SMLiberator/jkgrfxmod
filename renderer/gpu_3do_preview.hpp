#pragma once
// Opt-in GPU 3DO experiment; mesh data remains immutable after capture.
// Only this bridge is shared by the native hook and the OpenGL renderer.
#include <array>
#include <cstdint>
#include <memory>
#include <vector>
namespace jkgm {
    struct gpu_3do_geometry {
        std::vector<float> vertices; // local xyz
        std::vector<uint32_t> indices; // triangulated fan
    };
    struct gpu_3do_instance {
        uint64_t id = 0;
        std::shared_ptr<gpu_3do_geometry const> geometry;
        std::array<float,12> transform{}; // rdMatrix34 axes and world origin
    };
    bool queue_gpu_3do_mesh(void const *mesh, void const *matrix);
    std::vector<gpu_3do_instance> consume_gpu_3do_instances();
    void reset_gpu_3do_instances();
}
