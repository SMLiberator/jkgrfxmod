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
    return 0;
}
