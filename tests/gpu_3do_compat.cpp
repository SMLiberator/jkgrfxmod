#include "renderer/gpu_3do_compat.hpp"
#include <array>
#include <cassert>
int main() {
    using namespace jkgm;
    assert(gpu_3do_supports_geo_mode(4,4));
    assert(gpu_3do_supports_geo_mode(0,4)); // hidden Thing: valid, no draw
    assert(gpu_3do_supports_geo_mode(1,4)); // software-only vertices
    assert(gpu_3do_supports_geo_mode(2,4)); // software-only wireframe
    assert(gpu_3do_supports_geo_mode(3,4)); // Thing solid
    assert(gpu_3do_supports_geo_mode(4,3)); // Mesh solid
    assert(gpu_3do_supports_geo_mode(3,3));
    assert(!gpu_3do_supports_geo_mode(5,4));
    assert(gpu_3do_supports_geo_mode(4,2));
    assert(gpu_3do_supports_geo_mode(4,0));
    assert(!gpu_3do_supports_geo_mode(-1,4));
    assert(!gpu_3do_supports_geo_mode(4,-1));
    assert(!gpu_3do_supports_geo_mode(4,5));
    for(int face_mode=0;face_mode<=4;++face_mode) {
        assert(gpu_3do_supports_face_mode(face_mode));
        assert(gpu_3do_face_mode_draws_filled(face_mode)==(face_mode>=3));
    }
    assert(!gpu_3do_supports_face_mode(-1));
    assert(!gpu_3do_supports_face_mode(5));
    for(int thing_mode=0;thing_mode<=4;++thing_mode) {
        for(int mesh_mode=0;mesh_mode<=4;++mesh_mode) {
            assert(gpu_3do_geo_mode_draws_filled(thing_mode,mesh_mode)==
                   (thing_mode>=3 && mesh_mode>=3));
        }
    }
    std::array<float,12> id{{1,0,0, 0,1,0, 0,0,1, 0,0,0}};
    assert(!gpu_3do_reversed_winding(id));
    auto flip_x=id; flip_x[0]=-1;
    assert(gpu_3do_reversed_winding(flip_x));
    auto flip_xy=flip_x; flip_xy[4]=-1;
    assert(!gpu_3do_reversed_winding(flip_xy));
    auto flip_xyz=flip_xy; flip_xyz[8]=-1;
    assert(gpu_3do_reversed_winding(flip_xyz));
    auto nonuniform=id; nonuniform[0]=2;nonuniform[4]=3;nonuniform[8]=0.5f;
    assert(!gpu_3do_reversed_winding(nonuniform));
    auto swap=id; swap[0]=0;swap[1]=1;swap[3]=1;swap[4]=0;
    assert(gpu_3do_reversed_winding(swap));
    // JK's native criterion uses a mesh-local camera and face normal;
    // our early rejection must agree even for mirrored/scaled meshes.
    std::array<float,3> eye{};
    assert(gpu_3do_local_eye(id,eye));
    assert(eye[0]==0 && eye[1]==0 && eye[2]==0);
    auto moved=id; moved[11]=-5;
    assert(gpu_3do_local_eye(moved,eye));
    assert(eye[2]==5.0f);
    assert(gpu_3do_face_facing_camera(eye,{{0,0,0}},{{0,0,1}}));
    assert(!gpu_3do_face_facing_camera(eye,{{0,0,0}},{{0,0,-1}}));
    auto mirrored=moved; mirrored[0]=-2;
    assert(gpu_3do_local_eye(mirrored,eye));
    assert(eye[0]==0 && eye[2]==5.0f);
    auto scaled=moved;scaled[0]=2;scaled[4]=3;scaled[8]=0.5f;
    assert(gpu_3do_local_eye(scaled,eye));
    assert(eye[2]==10.0f);
    auto degenerate=scaled;degenerate[8]=0;
    assert(!gpu_3do_local_eye(degenerate,eye));
}
