#include "renderer/sprite_flare_visibility.hpp"
#include "renderer/sprite_flare_projection.hpp"
#include "renderer/sprite_orientation.hpp"
#include <algorithm>
#include <cassert>
#include <cmath>
#include <limits>
using namespace jkgm;
static bool near(float a,float b) {return std::fabs(a-b)<1.e-5f;}
int main() {
    sprite_flare_source_registry sources;
    std::array<float,3> const thing_position{{0.0f,10.0f,0.0f}};
    uint32_t const marker=sources.add(thing_position);
    assert(sprite_flare_is_marker(marker));
    assert(marker!=sprite_flare_source_magic);
    assert(!sprite_flare_is_marker(0U));
    std::array<float,3> resolved{};
    assert(sources.lookup(marker,resolved));
    assert(resolved==thing_position);
    assert(!sources.lookup(sprite_flare_source_magic,resolved));
    assert(!sources.lookup(0x12345678U,resolved));
    assert(sources.add(thing_position,false)==sprite_flare_source_magic);
    assert(sources.add({{std::numeric_limits<float>::quiet_NaN(),1.0f,2.0f}})
           ==sprite_flare_source_magic);
    assert(sources.size()==1U);

    std::array<float,12> const identity_camera{{
        1,0,0,0,1,0,0,0,1,0,0,0
    }};
    sprite_flare_center center{};
    assert(project_sprite_flare_thing_center(
        thing_position,identity_camera,1.0f,4.0f/3.0f,
        {{800.0f,600.0f}},center));
    assert(near(center.pixel[0],400.0f));
    assert(near(center.pixel[1],300.0f));
    assert(near(center.view_depth,320.0f));
    // The source stays at the same pixel/depth regardless of flare quad
    // dimensions, roll, clipping or its UVs: none are inputs here.
    assert(project_sprite_flare_thing_center(
        {{5.0f,10.0f,0.0f}},identity_camera,1.0f,4.0f/3.0f,
        {{800.0f,600.0f}},center));
    assert(near(center.pixel[0],600.0f));
    assert(near(center.view_depth,320.0f));
    assert(project_sprite_flare_thing_center(
        {{0.0f,10.0f,3.75f}},identity_camera,1.0f,4.0f/3.0f,
        {{800.0f,600.0f}},center));
    assert(near(center.pixel[1],450.0f));
    auto translated_camera=identity_camera;
    translated_camera[9]=-5.0f;
    assert(project_sprite_flare_thing_center(
        {{5.0f,10.0f,0.0f}},translated_camera,1.0f,4.0f/3.0f,
        {{800.0f,600.0f}},center));
    assert(near(center.pixel[0],400.0f));
    assert(!project_sprite_flare_thing_center(
        {{0.0f,-1.0f,0.0f}},identity_camera,1.0f,4.0f/3.0f,
        {{800.0f,600.0f}},center));
    assert(!project_sprite_flare_thing_center(
        {{30.0f,10.0f,0.0f}},identity_camera,1.0f,4.0f/3.0f,
        {{800.0f,600.0f}},center));
    // Former regression: a wall at Y=5 (scene depth 160) failed to hide
    // a flare at Y=10 because raw view Y=10 was compared against 160.
    // Prior negative test cases reset center, so reproject the source.
    assert(project_sprite_flare_thing_center(
        thing_position,identity_camera,1.0f,4.0f/3.0f,
        {{800.0f,600.0f}},center));
    assert(center.view_depth==320.0f);
    assert(160.0f+std::max(0.01f,320.0f*0.0005f)<center.view_depth);
    assert(!project_sprite_flare_thing_center(
        {{0.0f,2047.0f,0.0f}},identity_camera,1.0f,4.0f/3.0f,
        {{800.0f,600.0f}},center)); // 65504: R16F sky sentinel
    assert(project_sprite_flare_thing_center(
        {{0.0f,2046.0f,0.0f}},identity_camera,1.0f,4.0f/3.0f,
        {{800.0f,600.0f}},center));
    assert(near(center.view_depth,65472.0f));
    sources.clear();
    assert(!sources.lookup(marker,resolved));

    assert(sprite_center_occluded_flare==4U);
    assert(!sprite_is_decal(sprite_center_occluded_flare));
    sprite_axes thing{},camera{},axes{};
    assert(compute_sprite_axes(sprite_center_occluded_flare,thing,camera,axes));
    assert(near(axes.look[1],1.0f));
    // One source sample controls the whole billboard, without a fade.
    assert(sprite_flare_center_unoccluded(0.8f,0.4f));
    assert(sprite_flare_center_unoccluded(0.4f,0.4f));
    assert(!sprite_flare_center_unoccluded(0.2f,0.4f));
    assert(sprite_flare_center_unoccluded(0.39995f,0.4f));
    assert(!sprite_flare_center_unoccluded(-1.0f,0.4f));
    assert(!sprite_flare_center_unoccluded(1.0f,1.2f));
    assert(!sprite_flare_center_unoccluded(
        std::numeric_limits<float>::quiet_NaN(),0.5f));
    // Existing optional fade helper remains unchanged and unused here.
    sprite_flare_fade fade{};
    assert(near(fade.update(true,0.04f,0.08f),0.5f));
    assert(near(fade.update(true,0.08f,0.08f),1.0f));
    assert(near(fade.update(false,0.04f,0.08f),0.5f));
    assert(near(sprite_effective_opacity(0.4f,fade.visibility),0.2f));
    fade.reset(false);
    assert(near(fade.visibility,0.0f));
}
