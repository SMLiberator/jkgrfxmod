#include "renderer/sprite_gpu_quad.hpp"
#include "renderer/sprite_flare_projection.hpp"
#include <cassert>
#include <cmath>
#include <limits>
using namespace jkgm;
static bool eq(float a,float b) {return std::fabs(a-b)<0.0001f;}
int main() {
    gpu_3do_matrix34 thing{{
        1,0,0, 0,1,0, 0,0,1, 2,10,3
    }};
    gpu_3do_matrix34 camera{{
        1,0,0, 0,1,0, 0,0,1, 0,0,0
    }};
    float size[4]={1,1,1,1};
    float offset[3]={1,-2,2};
    gpu_3do_matrix34 out{};
    // Native offset X/Z still translates the billboard in camera axes.
    // Y is intentionally absent from geometric placement for GPU type 0/4.
    for(uint32_t type:{sprite_default,sprite_center_occluded_flare}) {
        assert(make_gpu_sprite_pose(
            type,thing,camera,size,offset,1,1,0,out));
        assert(eq(out[9],3));
        assert(eq(out[10],10));
        assert(eq(out[11],5));
        assert(eq(out[0],1) && eq(out[7],0) && eq(out[8],1));
        float const original_y=offset[1];
        offset[1]=3;
        gpu_3do_matrix34 farther{};
        assert(make_gpu_sprite_pose(
            type,thing,camera,size,offset,1,1,0,farther));
        assert(farther==out); // authored Y never changes size or placement
        offset[1]=original_y;
    }
    // Keep authored Y geometric for cylindrical and fixed SPRs.
    for(uint32_t type:{sprite_billboard,sprite_fixed}) {
        assert(make_gpu_sprite_pose(
            type,thing,camera,size,offset,1,1,0,out));
        assert(eq(out[10],8));
    }
    sprite_flare_center center{};
    assert(project_gpu_sprite_flare_center(
        {{3,10,5}},camera,1.0f,4.0f/3.0f,
        {{800,600}},-2.0f,center));
    assert(eq(center.view_depth,256));
    assert(eq(center.pixel[0],520));
    assert(eq(center.pixel[1],500));
    // Impossible effective depths fail instead of wrapping behind camera.
    assert(!project_gpu_sprite_flare_center(
        {{3,10,5}},camera,1.0f,4.0f/3.0f,
        {{800,600}},-11.0f,center));
}
