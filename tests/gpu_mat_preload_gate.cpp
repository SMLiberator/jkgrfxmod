#include "renderer/gpu_mat_preload_gate.hpp"
#include <cassert>
int main() {
    jkgm::gpu_mat_preload_gate g;
    assert(!g.first_attempt(0,0x123400,0));
    assert(!g.first_attempt(1,0,0));
    assert(!g.first_attempt(1,0x123400,16));
    assert(g.first_attempt(1,0x123400,0));
    assert(!g.first_attempt(1,0x123400,0));
    assert(g.first_attempt(1,0x123400,1)); // animated cel
    assert(g.first_attempt(1,0x123410,0)); // different MAT
    assert(g.first_attempt(2,0x123400,0)); // different world
    assert(g.size()==1);
    g.clear();
    assert(g.first_attempt(2,0x123400,0)); // reset
    return 0;
}
