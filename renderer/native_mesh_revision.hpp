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
    using handler = void(__cdecl *)(void *);
    using registrar = void(__cdecl *)(void *,handler,char const *);

    void __cdecl register_mutator(void *ctx,handler fn,char const *name);
    void begin(registrar fn);
    void finish();
    bool active();
    uint64_t current();
    // Profiling only; reading these counters does not change generation or
    // the conservative source-hash invalidation policy.
    mutator_counts take_mutator_counts();
    char const *mutator_name(std::size_t index);
    void report_untracked();
}
