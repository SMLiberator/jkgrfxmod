#pragma once
// Optional timings for the known patched JK 2.3 3DO render pipeline.
#include <cstdint>
namespace jkgm {
    struct native_3do_sample {
        uint64_t model_calls=0, model_drawn=0, mesh_calls=0;
        uint64_t face_calls=0, face_emitted=0;
        uint64_t model_ns=0, mesh_ns=0, face_ns=0; // inclusive; nested timers
        // Breakdown of the intercept, not an independent rendering pass.
        uint64_t model_hook_ns=0,model_engine_ns=0,model_engine_self_ns=0;
        uint64_t mesh_decal_ns=0,mesh_queue_ns=0,mesh_native_ns=0;
        uint64_t mesh_gpu_calls=0,mesh_native_calls=0;
        void add(native_3do_sample const &o) {
            model_calls+=o.model_calls;
            model_drawn+=o.model_drawn;
            mesh_calls+=o.mesh_calls;
            face_calls+=o.face_calls;
            face_emitted+=o.face_emitted;
            model_ns+=o.model_ns;
            mesh_ns+=o.mesh_ns;
            face_ns+=o.face_ns;
            model_hook_ns+=o.model_hook_ns;
            model_engine_ns+=o.model_engine_ns;
            model_engine_self_ns+=o.model_engine_self_ns;
            mesh_decal_ns+=o.mesh_decal_ns;
            mesh_queue_ns+=o.mesh_queue_ns;
            mesh_native_ns+=o.mesh_native_ns;
            mesh_gpu_calls+=o.mesh_gpu_calls;
            mesh_native_calls+=o.mesh_native_calls;
        }
    };
    native_3do_sample consume_native_3do_sample();
    bool native_3do_available();
}
