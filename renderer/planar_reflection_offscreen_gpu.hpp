#pragma once
// Independent, on-demand GPU mesh acquisition for Things omitted by the
// primary-camera 3DO traversal. The native implementation is in main.cpp,
// alongside the existing validated rdMesh snapshot and revision cache.
// No native draw callback is invoked; no rdCamera state is changed.
#include "gpu_3do_preview.hpp"
#include "planar_reflection_things.hpp"
#include <cstddef>
#include <vector>

namespace jkgm {
    struct planar_reflection_offscreen_stats {
        size_t considered=0,posed_things=0,pose_failures=0;
        size_t excluded_by_flags_or_geomode=0;
        size_t native_mesh_nodes=0,empty_or_invalid_meshes=0;
        size_t reused_gpu_meshes=0,new_gpu_snapshots=0;
        size_t material_unready=0,incomplete_things=0,ready_nodes=0;
    };
    struct planar_reflection_offscreen_gpu {
        std::vector<gpu_3do_instance> nodes;
        planar_reflection_offscreen_stats stats{};
    };
    // The caller controls the per-invocation Thing budget. The first version
    // is only called by an opt-in diagnostic, never the gameplay render pass.
    planar_reflection_offscreen_gpu acquire_planar_reflection_offscreen_gpu(
        std::vector<planar_reflection_model_thing> const &candidates,
        size_t max_things);
}
