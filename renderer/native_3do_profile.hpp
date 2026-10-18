#pragma once
// Optional timings for the known patched JK 2.3 3DO render pipeline.
#include <cstdint>
namespace jkgm {
    struct native_3do_sample {
        uint64_t model_calls=0, mesh_calls=0;
        uint64_t model_ns=0, mesh_ns=0; // inclusive, model contains mesh
        void add(native_3do_sample const &o) {
            model_calls+=o.model_calls;
            mesh_calls+=o.mesh_calls;
            model_ns+=o.model_ns;
            mesh_ns+=o.mesh_ns;
        }
    };
    native_3do_sample consume_native_3do_sample();
    bool native_3do_available();
}
