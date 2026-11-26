#pragma once
// Optional timings for the known patched JK 2.3 3DO render pipeline.
#include <cstdint>
namespace jkgm {
    struct native_3do_sample {
        uint64_t model_calls=0, model_drawn=0, mesh_calls=0;
        uint64_t model_lod0_forced=0; // stage-0 auto LOD: native rdThing GeoSet 0 override
        uint64_t model_occlusion_skipped=0;
        uint64_t model_occlusion_tests=0,model_occlusion_lookups=0;
        uint64_t model_occlusion_lookup_hits=0,model_occlusion_ns=0;
        uint64_t face_calls=0, face_emitted=0;
        uint64_t model_ns=0, mesh_ns=0, face_ns=0; // inclusive; nested timers
        // Breakdown of the intercept, not an independent rendering pass.
        uint64_t model_hook_ns=0,model_engine_ns=0,model_engine_self_ns=0;
        uint64_t mesh_decal_ns=0,mesh_queue_ns=0,mesh_native_ns=0;
        uint64_t mesh_gpu_calls=0,mesh_native_calls=0;
        uint64_t static_decal_capture_calls=0,static_decal_capture_skipped=0;
        uint64_t mesh_camera_reads=0;
        uint64_t audit_models=0;
        uint64_t audit_matched=0;
        uint64_t audit_unavailable=0;
        uint64_t audit_rejected=0;
        uint64_t audit_callbacks=0;
        uint64_t audit_order=0;
        uint64_t audit_matrix=0;
        uint64_t audit_modes=0;
        uint64_t audit_geoset=0;
        uint64_t audit_result=0;
        uint64_t audit_nested=0;
        void add(native_3do_sample const &o) {
            model_calls+=o.model_calls;
            model_lod0_forced+=o.model_lod0_forced;
            model_drawn+=o.model_drawn;
            model_occlusion_skipped+=o.model_occlusion_skipped;
            model_occlusion_tests+=o.model_occlusion_tests;
            model_occlusion_lookups+=o.model_occlusion_lookups;
            model_occlusion_lookup_hits+=o.model_occlusion_lookup_hits;
            model_occlusion_ns+=o.model_occlusion_ns;
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
            static_decal_capture_calls+=o.static_decal_capture_calls;
            static_decal_capture_skipped+=o.static_decal_capture_skipped;
            mesh_camera_reads+=o.mesh_camera_reads;
            audit_models+=o.audit_models;
            audit_matched+=o.audit_matched;
            audit_unavailable+=o.audit_unavailable;
            audit_rejected+=o.audit_rejected;
            audit_callbacks+=o.audit_callbacks;
            audit_order+=o.audit_order;
            audit_matrix+=o.audit_matrix;
            audit_modes+=o.audit_modes;
            audit_geoset+=o.audit_geoset;
            audit_result+=o.audit_result;
            audit_nested+=o.audit_nested;
        }
    };
    native_3do_sample consume_native_3do_sample();
    bool native_3do_available();
}
