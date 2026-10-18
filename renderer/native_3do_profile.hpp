#pragma once
// Optional timings for the known patched JK 2.3 3DO render pipeline.
#include <cstdint>
namespace jkgm {
    struct native_3do_sample {
        uint64_t model_calls=0, model_drawn=0, mesh_calls=0;
        uint64_t face_calls=0, face_emitted=0;
        uint64_t model_ns=0, mesh_ns=0, face_ns=0; // inclusive; nested timers
        void add(native_3do_sample const &o) {
            model_calls+=o.model_calls;
            model_drawn+=o.model_drawn;
            mesh_calls+=o.mesh_calls;
            face_calls+=o.face_calls;
            face_emitted+=o.face_emitted;
            model_ns+=o.model_ns;
            mesh_ns+=o.mesh_ns;
            face_ns+=o.face_ns;
        }
    };
    native_3do_sample consume_native_3do_sample();
    bool native_3do_available();
}
