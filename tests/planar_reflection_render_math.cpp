#include "renderer/planar_reflection_render_math.hpp"
#include <cassert>
#include <cmath>
#include <limits>
int main() {
    using namespace jkgm;
    engine_camera main{};
    main.valid=true;
    main.position={{0,0,10}};
    main.right={{1,0,0}};
    main.forward={{0,1,0}};
    main.up={{0,0,1}};
    main.fov=90.0f;
    planar_reflection_plane plane{};
    plane.normal={{0,0,1}};
    plane.offset=0;
    auto const mirrored=make_planar_reflection_camera(main,plane);
    assert(mirrored.valid && mirrored.position[2]==-10);
    auto p=make_planar_reflection_render_setup(1920,1080,0.5f,main,mirrored,plane);
    assert(p.valid && p.width==960 && p.height==540);
    // Reflected camera (-10) is rejected; real geometry at +10 survives.
    assert(std::fabs(p.clip_view[3]+10.0f)<1.e-5f);
    assert(p.clip_view[2]==-1.0f);
    float const fake_view_z=-20.0f;
    assert(p.clip_view[2]*fake_view_z+p.clip_view[3]>0.0f);
    assert(p.clip_view[3]<0.0f);
    auto q=make_planar_reflection_render_setup(1025,769,0.25f,main,mirrored,plane);
    assert(q.valid && q.width==257 && q.height==193);
    auto low=make_planar_reflection_render_setup(
        1920,1080,0.2f,main,mirrored,plane);
    assert(low.valid && low.width==384 && low.height==216);
    auto min_scale=make_planar_reflection_render_setup(
        1920,1080,0.125f,main,mirrored,plane);
    assert(min_scale.valid && min_scale.width==240 && min_scale.height==135);
    auto r=make_planar_reflection_render_setup(1920,1080,1.0f,main,mirrored,plane);
    assert(r.valid && r.width==1920 && r.height==1080);
    main.position[2]=0;
    assert(!make_planar_reflection_render_setup(
        1920,1080,0.5f,main,mirrored,plane).valid);
    main.position[2]=10;
    assert(!make_planar_reflection_render_setup(
        1920,1080,0.1f,main,mirrored,plane).valid);
    assert(!make_planar_reflection_render_setup(
        1920,1080,std::numeric_limits<float>::quiet_NaN(),
        main,mirrored,plane).valid);
    return 0;
}
