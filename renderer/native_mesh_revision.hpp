#pragma once

#include <cstdint>

// Wrap the 16 JK.dll model/material COG mutators at registration time.
// The original handler and registrar ABI are preserved; callers query whether
// the optimized GPU cache revision fast path remains safe for this session.
namespace mesh_revision {
    using handler = void(__cdecl *)(void *);
    using registrar = void(__cdecl *)(void *,handler,char const *);

    void __cdecl register_mutator(void *ctx,handler fn,char const *name);
    void begin(registrar fn);
    void finish();
    bool active();
    uint64_t current();
    void report_untracked();
}
