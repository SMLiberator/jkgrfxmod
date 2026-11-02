#pragma once
// Selective legacy D3D fallback while GPU-cached SithWorld owns architecture.
// Native world batches also contain particle Things, lightsaber polylines,
// uncached 3DOs and sprites. Suppressing the entire batch hides those Things.
// Use native D3D MAT handles as a conservative ownership discriminator:
// when a handle belongs to a visible SithWorld surface, the GPU world owns it;
// otherwise the existing native path remains available for non-world draws.
// Material sharing is an unavoidable ambiguity without a native origin tag.
#include <cstdint>
#include <set>

namespace jkgm {
struct world_cache_native_fallback {
    std::set<uint32_t> world_texture_handles;
    bool world_uses_untextured=false;

    bool allows(uint32_t native_handle) const
    {
        if(native_handle==0U)return !world_uses_untextured;
        return world_texture_handles.count(native_handle)==0U;
    }
};
} // namespace jkgm
