#pragma once

// Signature-gated sithSound_Load Detours hook for JK's compressed format
// HostServices bridge. request/detach only enqueue operations: main.cpp owns
// the enclosing Detours transaction, installed state, and service teardown.
namespace jkgm::native_compressed_sound_hook {
    bool request();
    void detach();
}
