#pragma once
// Development-only native 3DO GPU bypass. Each immutable mesh is copied once,
// including UV seams and per-face material references. Transforms are dynamic.
#include <array>
#include <cstdint>
#include <memory>
#include <vector>

namespace jkgm {
    struct gpu_3do_vertex {
        float x,y,z;
        float u,v; // source MAT UVs, in original texture pixels
        float nx,ny,nz; // mesh-local face normal
    };
    static_assert(sizeof(gpu_3do_vertex)==32,"Unexpected GPU vertex stride");

    struct gpu_3do_group {
        uint32_t offset=0, count=0; // index-buffer range
        uintptr_t material=0;       // native JK rdMaterial pointer
        int32_t cel=-1;             // -1 means follow current material cel
        uint32_t face_type=0; // RD_FF_DOUBLE_SIDED=0x1, translucent=0x2
        std::array<float,3> centroid{}; // face-local center for transparency sorting
    };
    struct gpu_3do_geometry {
        std::vector<gpu_3do_vertex> vertices;
        std::vector<uint32_t> indices;
        std::vector<gpu_3do_group> groups;
    };
    struct gpu_3do_instance {
        uint64_t id=0;
        std::shared_ptr<gpu_3do_geometry const> geometry;
        std::array<float,12> transform{}; // 3DO node matrix
    };
    struct gpu_3do_native_stats {
        uint64_t attempted=0,culled=0,queued=0,snapshots=0,ready_checks=0;
    };
    gpu_3do_native_stats consume_gpu_3do_native_stats();
    bool queue_gpu_3do_mesh(void const *mesh,void const *matrix);
    std::vector<gpu_3do_instance> consume_gpu_3do_instances();
    void reset_gpu_3do_instances();
}
