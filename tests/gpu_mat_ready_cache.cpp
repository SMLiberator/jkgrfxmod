#include "renderer/gpu_mat_ready_cache.hpp"
#include <cassert>
int main() {
    jkgm::gpu_mat_ready_cache c;
    assert(!c.contains(1,1,0x10000,0));
    c.mark_ready(1,1,0x10000,0);
    assert(c.contains(1,1,0x10000,0));
    assert(!c.contains(1,1,0x10000,1)); // animated MAT cel
    assert(!c.contains(1,1,0x20000,0)); // different MAT
    c.mark_ready(1,1,0x10000,1);
    assert(c.contains(1,1,0x10000,1));
    assert(!c.contains(1,2,0x10000,0)); // upload invalidation
    c.mark_ready(1,2,0x10000,0);
    assert(!c.contains(2,2,0x10000,0)); // next frame
    c.mark_ready(2,2,0x10000,0);
    c.clear();
    assert(!c.contains(2,2,0x10000,0)); // world reset

    // Positive readiness can become stale within the same frame when
    // a texture handle is recycled or its backing D3D image is reloaded.
    assert(!jkgm::gpu_mat_mesh_needs_recheck(20,100,20,100));
    assert(jkgm::gpu_mat_mesh_needs_recheck(20,100,20,101));
    assert(jkgm::gpu_mat_mesh_needs_recheck(20,100,21,100));
    // Negative readiness also retries immediately after a native upload.
    assert(jkgm::gpu_mat_mesh_needs_recheck(20,100,20,102));
    return 0;
}
