#pragma once

#include <array>
#include <cstddef>
#include <cstdint>

// Wrap the 16 JK.dll model/material COG mutators at registration time.
// The original handler and registrar ABI are preserved; callers query whether
// the optimized GPU cache revision fast path remains safe for this session.
namespace mesh_revision {
    constexpr std::size_t mutator_count=16U;
    using mutator_counts=std::array<uint64_t,mutator_count>;

    // A COG model mutation is not necessarily a mutation of an rdMesh.
    // Preserve the original global generation for collision/model consumers,
    // but the GPU cache only needs a targeted scan when existing face
    // materials or arbitrary model source bytes might have changed.
    enum source_effect : unsigned {
        source_none=0U,
        source_materials=1U,
        source_full=2U
    };
    constexpr unsigned source_effect_for(std::size_t verb)
    {
        // Index order matches the 16 verb names in native_mesh_revision.cpp.
        // Mesh replacement/clearing and node swaps change rdMesh headers or
        // references: these are validated on their next encounter.
        switch(verb) {
            case 2U: case 3U: case 9U:
                return source_materials; // in-place face.material edits
            case 12U: case 13U:
                return source_full; // overwrite/reload may alter contents in place
            default:
                return source_none; // clone, append, pointer/header swaps, clears
        }
    }
    using handler = void(__cdecl *)(void *);
    using registrar = void(__cdecl *)(void *,handler,char const *);

    void __cdecl register_mutator(void *ctx,handler fn,char const *name);
    void begin(registrar fn);
    void finish();
    bool active();
    uint64_t current(); // unchanged global generation for collision caches
    uint64_t source_current(); // GPU source fast path; targeted changes do not bump
    unsigned take_pending_source_effects(); // consumed before next GPU lookup
    // Profiling only; reading these counters does not change generation or
    // the conservative source-hash invalidation policy.
    mutator_counts take_mutator_counts();
    char const *mutator_name(std::size_t index);
    void report_untracked();
}
