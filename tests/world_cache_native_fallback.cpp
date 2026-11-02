#include "renderer/world_cache_native_fallback.hpp"
#include <cassert>
#include <cstdint>

int main()
{
    jkgm::world_cache_native_fallback fallback{};
    // No world MAT may claim a native Thing's unique material handle.
    fallback.world_texture_handles.insert(14U); // cached wall
    fallback.world_texture_handles.insert(88U); // cached water
    assert(!fallback.allows(14U));
    assert(!fallback.allows(88U));
    assert(fallback.allows(42U)); // particle or lightsaber blade
    assert(fallback.allows(0U)); // solid-color particle in textured world

    // A visible solid-color world face can also use native handle zero.
    // Do not reintroduce its overdraw merely to recover solid particles.
    fallback.world_uses_untextured=true;
    assert(!fallback.allows(0U));
    assert(fallback.allows(42U));

    // A world load / cache invalidation must not inherit the prior world's
    // material ownership information.
    fallback={};
    assert(fallback.allows(14U));
    assert(fallback.allows(0U));
    return 0;
}
