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
        float nx,ny,nz; // mesh-local face normal (G-buffer)
        float static_light; // immutable .3DO vertex intensity
        float lnx,lny,lnz; // mesh-local vertex normal for Gouraud illumination
        float fax,fay,faz; // face's first vertex: JK diffuse light anchor
        float face_extra; // .3DO per-face extra light
        float light_mode; // 0=fully lit, 1=not lit, 2=diffuse, 3=Gouraud
        float cr,cg,cb; // immutable RGB vertex tint from patched JK .rgbx mesh
    };
    static_assert(sizeof(gpu_3do_vertex)==80,"Unexpected GPU vertex stride");

    constexpr unsigned gpu_3do_max_lights=16;
    struct gpu_3do_light {
        std::array<float,3> local_position{};
        float radius=0.0f;
        float intensity=0.0f;
    };

    struct gpu_3do_face_plane {
        std::array<float,3> anchor{};
        std::array<float,3> normal{};
    };
    struct gpu_3do_group {
        uint32_t offset=0, count=0; // index-buffer range
        uintptr_t material=0;       // native JK rdMaterial pointer
        int32_t cel=-1;             // -1 means follow current material cel
        uint32_t face_type=0; // RD_FF_DOUBLE_SIDED=0x1, translucent=0x2
        std::array<float,3> centroid{}; // face-local center for transparency sorting
        uint32_t plane_start=0, plane_count=0; // planes used for CPU backface rejection
    };
    struct gpu_3do_geometry {
        std::vector<gpu_3do_face_plane> face_planes;
        std::vector<gpu_3do_vertex> vertices;
        std::vector<uint32_t> indices;
        std::vector<gpu_3do_group> groups;
    };
    struct gpu_3do_instance {
        uint64_t id=0;
        std::shared_ptr<gpu_3do_geometry const> geometry;
        std::array<float,12> transform{}; // 3DO node matrix
        // JK's rdCamera light list is Thing-dependent: snapshot when the mesh
        // hook runs, NOT at present(), after another Thing changes it.
        float ambient=1.0f, attenuation=0.0f;
        float effective_light_mode=3.0f; // JK's effective per-Thing lighting cap
        unsigned num_lights=0;
        std::array<gpu_3do_light,gpu_3do_max_lights> lights{};
    };
    struct gpu_3do_native_stats {
        uint64_t attempted=0,culled=0,queued=0,snapshots=0,ready_checks=0;
        uint64_t text_native=0,geometry_native=0,snapshot_native=0;
        uint64_t material_native=0,lighting_native=0;
    };
    gpu_3do_native_stats consume_gpu_3do_native_stats();
    bool queue_gpu_3do_mesh(void const *mesh,void const *matrix);
    std::vector<gpu_3do_instance> consume_gpu_3do_instances();
    void reset_gpu_3do_instances();
}
