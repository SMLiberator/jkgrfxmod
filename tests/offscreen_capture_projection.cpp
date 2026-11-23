#include "renderer/offscreen_capture_projection.hpp"
#include <cassert>
#include <cmath>
#include <limits>
static bool eq(float a,float b) { return std::fabs(a-b)<1e-4f; }
int main() {
    using namespace jkgm;
    auto p=make_offscreen_capture_projection(512,256,90.0f,0.0f,128.0f);
    assert(p.valid && !p.orthographic);
    assert(eq(p.aspect,2.0f) && eq(p.half_horizontal,1.0f));
    auto c=offscreen_capture_clip_coordinates(p,{{1.0f,4.0f,0.0f}});
    assert(eq(c[0]/c[3],0.25f));
    assert(eq(c[3],4.0f));
    auto o=make_offscreen_capture_projection(512,256,0.0f,8.0f,128.0f);
    assert(o.valid && o.orthographic && eq(o.half_horizontal,8.0f));
    auto middle=offscreen_capture_clip_coordinates(o,{{0.0f,64.0f,0.0f}});
    assert(eq(middle[0],0) && eq(middle[1],0));
    assert(eq(middle[2],0) && eq(middle[3],1));
    auto edge=offscreen_capture_clip_coordinates(o,{{8.0f,64.0f,4.0f}});
    assert(eq(edge[0],1) && eq(edge[1],1));
    // CameraToMat's 128x128 still captures must accept FOV=0 as a
    // one-world-unit orthographic view and positive FOV as perspective.
    auto photo_ortho=make_offscreen_capture_projection(
        128,128,0.0f,1.0f,128.0f);
    assert(photo_ortho.valid && photo_ortho.orthographic);
    assert(eq(photo_ortho.aspect,1.0f) &&
           eq(photo_ortho.half_horizontal,0.5f));
    auto photo_mid=offscreen_capture_clip_coordinates(
        photo_ortho,{{0.0f,4.0f,0.0f}});
    assert(eq(photo_mid[0],0.0f) && eq(photo_mid[1],0.0f));
    assert(eq(photo_mid[3],1.0f));
    auto photo_perspective=make_offscreen_capture_projection(
        128,128,75.0f,1.0f,128.0f);
    assert(photo_perspective.valid && !photo_perspective.orthographic);
    assert(photo_perspective.half_horizontal>0.0f);
    assert(!make_offscreen_capture_projection(512,256,0,0,128).valid);
    assert(!make_offscreen_capture_projection(0,256,90,0,128).valid);
    assert(!make_offscreen_capture_projection(512,256,180,0,128).valid);
    assert(!make_offscreen_capture_projection(512,256,-1,0,128).valid);
    assert(!make_offscreen_capture_projection(512,256,90,0,0).valid);
    assert(!make_offscreen_capture_projection(512,256,90,0,
        std::numeric_limits<float>::quiet_NaN()).valid);
    assert(!make_offscreen_capture_projection(512,256,0,
        std::numeric_limits<float>::infinity(),128).valid);
    return 0;
}
