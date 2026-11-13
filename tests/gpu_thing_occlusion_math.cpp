#include "renderer/gpu_thing_occlusion_math.hpp"
#include <cassert>
#include <limits>
int main() {
    using namespace jkgm;
    engine_camera cam{};cam.valid=true;cam.fov=90.0f;
    gpu_thing_occlusion_bound b{};
    b.center={{0.0f,5.0f,0.0f}};
    b.radius=1.0f;b.valid=true;
    assert(gpu_thing_occlusion_bound_valid(b));
    assert(!gpu_thing_occlusion_camera_inside(b,cam));
    auto moved=b;moved.center[0]+=0.01f;
    assert(gpu_thing_occlusion_same_bound(b,moved));
    moved.center[0]+=0.1f;
    assert(!gpu_thing_occlusion_same_bound(b,moved));
    auto scaled=b;scaled.radius+=0.02f;
    assert(!gpu_thing_occlusion_same_bound(b,scaled));
    auto invalid=b;invalid.radius=0;
    assert(!gpu_thing_occlusion_bound_valid(invalid));
    invalid=b;invalid.radius=std::numeric_limits<float>::quiet_NaN();
    assert(!gpu_thing_occlusion_bound_valid(invalid));
    assert(!gpu_thing_occlusion_camera_inside(invalid,cam)==false);
    cam.position={{0,5,0}};
    assert(gpu_thing_occlusion_camera_inside(b,cam));
    engine_camera prior=cam;
    assert(gpu_thing_occlusion_camera_compatible(cam,prior));
    prior.position[0]+=0.26f;
    assert(!gpu_thing_occlusion_camera_compatible(cam,prior));
    prior=cam;prior.forward[0]+=0.05f;
    assert(!gpu_thing_occlusion_camera_compatible(cam,prior));
    prior=cam;prior.fov+=0.1f;
    assert(!gpu_thing_occlusion_camera_compatible(cam,prior));
    return 0;
}
