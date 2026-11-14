#include "renderer/gpu_thing_occlusion_math.hpp"
#include "renderer/native_occlusion_bridge.hpp"
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
    auto near_sphere=sphere;
    near_sphere.center[1]=1.05f;
    assert(!gpu_thing_occlusion_project_sphere_rect(
        near_sphere,view,1.0f,1.0f,rect));
    assert(!rect.valid);
    auto behind=sphere;
    behind.center[1]= -5.0f;
    assert(!gpu_thing_occlusion_project_sphere_rect(
        behind,view,1.0f,1.0f,rect));
    auto offscreen=sphere;
    offscreen.center[0]=100.0f;
    assert(!gpu_thing_occlusion_project_sphere_rect(
        offscreen,view,1.0f,1.0f,rect));
    // Native hook and GPU renderer exchange only immutable snapshots.
    // Verify skipped candidates remain available for subsequent queries.
    auto &bridge=get_native_occlusion_bridge();
    bridge.clear();
    native_occlusion_candidate candidate{};
    candidate.owner=0x1234000056ULL;
    candidate.model=0x1234U;
    candidate.bound=sphere;
    bridge.register_candidate(candidate);
    bridge.register_candidate(candidate); // deduplicated per Thing
    auto candidates=bridge.take_candidates();
    assert(candidates.size()==1U);
    assert(candidates[0].model==candidate.model);
    assert(bridge.take_candidates().empty());
    native_occlusion_result result{};
    result.owner=candidate.owner;
    result.model=candidate.model;
    result.bound=sphere;
    result.camera=prior; // valid camera from above
    bridge.publish(0x102030U,{result});
    native_occlusion_result lookup{};
    uintptr_t seen_world=0;
    assert(bridge.lookup(candidate.owner,candidate.model,
                         sphere,lookup,seen_world));
    assert(seen_world==0x102030U);
    assert(lookup.owner==candidate.owner);
    auto different_bound=sphere;
    different_bound.center[0]+=0.25f;
    assert(!bridge.lookup(candidate.owner,candidate.model,
                          different_bound,lookup,seen_world));
    assert(!bridge.lookup(candidate.owner,candidate.model+1U,
                          sphere,lookup,seen_world));
    bridge.clear();
    assert(!bridge.lookup(candidate.owner,candidate.model,
                          sphere,lookup,seen_world));
    return 0;
}
