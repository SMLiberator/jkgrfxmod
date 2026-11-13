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
    // Native rdCamera-style view matrix: identity camera, forward +Y.
    gpu_3do_matrix34 view{};
    view[0]=1.0f;view[4]=1.0f;view[8]=1.0f;
    gpu_thing_occlusion_screen_rect rect{};
    auto sphere=b;
    sphere.center={{0.0f,5.0f,0.0f}};
    assert(gpu_thing_occlusion_project_sphere_rect(
        sphere,view,1.0f,1.0f,rect));
    assert(rect.valid && rect.center_forward==5.0f);
    // Perspective tangents enclose the sphere more tightly/correctly
    // than projecting (center +/- radius) at the center's depth.
    assert(rect.left< -0.2f && rect.right>0.2f);
    assert(rect.bottom< -0.2f && rect.top>0.2f);
    auto shifted=sphere;
    shifted.center[0]=3.0f;
    assert(gpu_thing_occlusion_project_sphere_rect(
        shifted,view,1.0f,1.0f,rect));
    assert(rect.left<0.4f && rect.right>0.8f);
    auto near=sphere;
    near.center[1]=1.05f;
    assert(!gpu_thing_occlusion_project_sphere_rect(
        near,view,1.0f,1.0f,rect));
    assert(!rect.valid);
    auto behind=sphere;
    behind.center[1]= -5.0f;
    assert(!gpu_thing_occlusion_project_sphere_rect(
        behind,view,1.0f,1.0f,rect));
    auto offscreen=sphere;
    offscreen.center[0]=100.0f;
    assert(!gpu_thing_occlusion_project_sphere_rect(
        offscreen,view,1.0f,1.0f,rect));
    return 0;
}
