#pragma once

#include <cstdint>

// Wrap known JK.dll COG model mutators at registration time. The native
// call ABI and stack contents are preserved. Model-scoped GPU epochs avoid
// rehashing unrelated meshes; collision queries retain a global epoch.
namespace mesh_revision {
    using handler = void(__cdecl *)(void *);
    using registrar = void(__cdecl *)(void *,handler,char const *);

    void __cdecl register_mutator(void *ctx,handler fn,char const *name);
    void begin(registrar fn);
    void finish();
    bool active();
    uint64_t current(); // global generation for type-3 collision queries
    uint64_t current_for_model(uintptr_t model); // GPU meshes only
    void reset_cache_state(); // clear epochs with the GPU source cache
}
