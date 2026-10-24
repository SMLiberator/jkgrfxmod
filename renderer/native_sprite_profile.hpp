#pragma once
// Opt-in native JK23 SPR submission timings and counts.
// Unlike GPU mesh draw timings, native_ns measures the inclusive rdSprite_Draw
// call on the game thread (projection, clipping and native face submission).
#include <array>
#include <cstdint>

namespace jkgm {
struct native_sprite_sample {
    std::array<uint64_t,5> type_calls{}; // authored types 0..4
    uint64_t other_type_calls=0;
    uint64_t hidden_calls=0;          // zero size or opacity
    uint64_t native_calls=0;          // RealNativeSprite invocations
    uint64_t native_success=0;        // nonzero native return
    uint64_t native_ns=0;
    uint64_t native_proc_faces=0;     // successfully appended SPR proc faces
    uint64_t projected_gpu=0;         // type-3 projected GPU geometry queued
    uint64_t projected_warmups=0;     // masked native fallback for MAT residency

    void add(native_sprite_sample const &o) {
        for(size_t i=0;i<type_calls.size();i++)
            type_calls[i]+=o.type_calls[i];
        other_type_calls+=o.other_type_calls;
        hidden_calls+=o.hidden_calls;
        native_calls+=o.native_calls;
        native_success+=o.native_success;
        native_ns+=o.native_ns;
        native_proc_faces+=o.native_proc_faces;
        projected_gpu+=o.projected_gpu;
        projected_warmups+=o.projected_warmups;
    }
};

native_sprite_sample consume_native_sprite_sample();
bool native_sprite_available();
}
