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
    assert(rect.valid && rect.near_forward==4.0f);
    assert(rect.center_forward==5.0f);
    assert(rect.center_forward>rect.near_forward);
    // Perspective tangents enclose the sphere more tightly/correctly
    // than projecting (center +/- radius) at the center's depth.
    assert(rect.left< -0.2f && rect.right>0.2f);
    assert(rect.bottom< -0.2f && rect.top>0.2f);
    // Camera rotation/translation MUST use the same column-vector
    // convention as gpu_3do_native_transform_point and the GPU shader.
    // An identity-only test incorrectly passed the old transposed basis.
    gpu_3do_matrix34 yaw_view{{
        0.0f,1.0f,0.0f,
       -1.0f,0.0f,0.0f,
        0.0f,0.0f,1.0f,
        2.0f,3.0f,4.0f}};
    gpu_thing_occlusion_bound rotated_sphere{};
    rotated_sphere.center={{5.0f,-1.0f,-3.0f}};
    rotated_sphere.radius=1.0f;
    rotated_sphere.valid=true;
    auto const yaw_center=gpu_3do_native_transform_point(
        yaw_view,rotated_sphere.center);
    assert(yaw_center[0]==3.0f && yaw_center[1]==8.0f &&
           yaw_center[2]==1.0f);
    assert(gpu_thing_occlusion_project_sphere_rect(
        rotated_sphere,yaw_view,1.0f,1.0f,rect));
    assert(rect.near_forward==7.0f);
    assert(rect.center_forward==8.0f);
    assert(rect.left<3.0f/8.0f && rect.right>3.0f/8.0f);
    assert(rect.bottom<1.0f/8.0f && rect.top>1.0f/8.0f);
    // A pitched camera catches a second transposition that swaps
    // forward and up, even if yaw alone happens to align.
    gpu_3do_matrix34 pitch_view{{
        1.0f,0.0f,0.0f,
        0.0f,0.6f,-0.8f,
        0.0f,0.8f,0.6f,
        0.0f,0.0f,0.0f}};
    gpu_thing_occlusion_bound pitched_sphere{};
    pitched_sphere.center={{1.0f,10.0f,5.0f}};
    pitched_sphere.radius=1.0f;
    pitched_sphere.valid=true;
    auto const pitch_center=gpu_3do_native_transform_point(
        pitch_view,pitched_sphere.center);
    assert(std::fabs(pitch_center[0]-1.0f)<0.0001f);
    assert(std::fabs(pitch_center[1]-10.0f)<0.0001f);
    assert(std::fabs(pitch_center[2]+5.0f)<0.0001f);
    assert(gpu_thing_occlusion_project_sphere_rect(
        pitched_sphere,pitch_view,1.0f,1.0f,rect));
    assert(std::fabs(rect.near_forward-9.0f)<0.0001f);
    assert(std::fabs(rect.center_forward-10.0f)<0.0001f);
    assert(rect.left<0.1f && rect.right>0.1f);
    assert(rect.bottom< -0.5f && rect.top> -0.5f);
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
    // SPR bounds use the ACTUAL rendered quad pose (including camera
    // alignment, authored scaling, roll and offsets), not rdModel3 radius.
    gpu_3do_matrix34 sprite_pose{{
        3.0f,0.0f,0.0f,
        0.0f,1.0f,0.0f,
        0.0f,0.0f,4.0f,
        2.0f,10.0f,3.0f
    }};
    auto sprite_bound=gpu_sprite_occlusion_bound(sprite_pose);
    assert(gpu_thing_occlusion_bound_valid(sprite_bound));
    assert(sprite_bound.center[0]==2.0f &&
           sprite_bound.center[1]==10.0f &&
           sprite_bound.center[2]==3.0f);
    assert(sprite_bound.radius>7.0f);
    // All transformed corners must fit, even with nonorthogonal axes.
    sprite_pose[6]=1.0f; // shear the up axis toward the right
    sprite_bound=gpu_sprite_occlusion_bound(sprite_pose);
    for(int sx : {-1,1})for(int sz : {-1,1}) {
        double len2=0.0;
        for(int axis=0;axis<3;++axis) {
            auto const d=double(sx)*sprite_pose[axis]+
                         double(sz)*sprite_pose[6+axis];
            len2+=d*d;
        }
        assert(std::sqrt(len2)<sprite_bound.radius);
    }
    auto scaled_sprite=sprite_pose;
    for(int axis=0;axis<3;++axis)scaled_sprite[axis]*=2.0f;
    auto const changed=gpu_sprite_occlusion_bound(scaled_sprite);
    assert(!gpu_thing_occlusion_same_bound(sprite_bound,changed));
    auto moved_sprite=sprite_pose;
    moved_sprite[9]+=1.0f;
    assert(!gpu_thing_occlusion_same_bound(
        sprite_bound,gpu_sprite_occlusion_bound(moved_sprite)));
    auto invalid_sprite=sprite_pose;
    invalid_sprite[6]=std::numeric_limits<float>::quiet_NaN();
    assert(!gpu_thing_occlusion_bound_valid(
        gpu_sprite_occlusion_bound(invalid_sprite)));
    // Near-plane SPRs must fail open rather than query a truncated quad.
    gpu_3do_matrix34 near_sprite_pose=sprite_pose;
    near_sprite_pose[10]=1.0f;
    assert(!gpu_thing_occlusion_project_sphere_rect(
        gpu_sprite_occlusion_bound(near_sprite_pose),
        view,1.0f,1.0f,rect));
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
