#include "renderer/gpu_3do_frustum.hpp"
#include <cassert>
int main() {
    jkgm::gpu_3do_native_frustum f{};
    f.clip_far=1;f.near_plane=1;f.far_plane=100;
    f.far_top=1;f.bottom=-1;f.far_left=-1;f.right=1;
    assert(!jkgm::gpu_3do_sphere_outside(f,true,0,10,0,1));
    assert(jkgm::gpu_3do_sphere_outside(f,true,0,-10,0,1));
    assert(jkgm::gpu_3do_sphere_outside(f,true,0,110,0,1));
    assert(jkgm::gpu_3do_sphere_outside(f,true,100,10,0,1));
    assert(jkgm::gpu_3do_sphere_outside(f,true,-100,10,0,1));
    assert(jkgm::gpu_3do_sphere_outside(f,true,0,10,100,1));
    assert(jkgm::gpu_3do_sphere_outside(f,true,0,10,-100,1));
    assert(!jkgm::gpu_3do_sphere_outside(f,true,10,10,0,1));
    assert(!jkgm::gpu_3do_sphere_outside(f,true,0,100,0,2));
    assert(!jkgm::gpu_3do_sphere_outside(f,true,0,110,0,20));
    return 0;
}
