#pragma once

// Native sector-tint isolation at the verified rdSetColorEffects entry.
// request() and detach() queue operations in main.cpp's Detours transaction.
// Only main.cpp marks the hook as installed after a successful commit.
namespace jkgm::native_sector_color_hook {
    bool request(bool gpu_cache_enabled);
    void detach();
}
