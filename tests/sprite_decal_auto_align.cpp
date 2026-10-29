#include "../renderer/sprite_decal_auto_align.hpp"
#include "../renderer/sprite_decal_attachment.hpp"
#include <cassert>
#include <cmath>
int main() {
    using namespace jkgm;
    decal_world_geometry scene{};
    scene.valid_world=true;
    decal_world_surface_geometry floor{};
    floor.normal={{0,0,1}};
    floor.polygon={{{-1,-1,0}},{{1,-1,0}},{{1,1,0}},{{-1,1,0}}};
    floor.bounds_min={{-1,-1,0}};
    floor.bounds_max={{1,1,0}};
    decal_world_surface_geometry wall{};
    wall.normal={{0,-1,0}};
    wall.polygon={{{-1,0,0}},{{-1,0,2}},{{1,0,2}},{{1,0,0}}};
    wall.bounds_min={{-1,0,0}};
    wall.bounds_max={{1,0,2}};
    scene.receivers={floor,wall};
    scene.acceleration.build(scene.receivers);
    decal_world_geometry empty{};
    auto const wallhit=find_nearest_decal_surface(
        scene,empty,{{0,-0.012f,0.015f}},{{0,1,0}});
    assert(wallhit.valid);
    assert(std::fabs(wallhit.normal[1]+1.0f)<1.e-5f);
    auto const floorhit=find_nearest_decal_surface(
        scene,empty,{{0,-0.1f,0.02f}},{{0,0,-1}});
    assert(floorhit.valid && floorhit.normal[2]>0.9f);
    // A camera-facing scoring preference previously picked the farther
    // side of a thin limb (0.018 away) over its true closest surface
    // (0.002 away). Moving decals use zero incoming bias.
    decal_world_geometry limb{};
    limb.valid_world=true;
    auto back=floor;
    back.normal={{0,0,-1}};
    for(auto &point:back.polygon)point[2]+=0.02f;
    back.bounds_min[2]=back.bounds_max[2]=0.02f;
    limb.receivers={floor,back};
    limb.acceleration.build(limb.receivers);
    auto biased=find_nearest_decal_surface(
        limb,empty,{{0,0,0.002f}},{{0,0,1}},0.05f);
    assert(biased.valid && biased.normal[2]<0.0f);
    auto unbiased=find_nearest_decal_surface(
        limb,empty,{{0,0,0.002f}},{{0,0,0}},0.05f);
    assert(unbiased.valid && unbiased.normal[2]>0.0f);
    assert(!find_nearest_decal_surface(scene,empty,{{10,10,10}},
                                     {{0,1,0}}).valid);
    sprite_axes axes{};
    sprite_axes aligned{};
    assert(sprite_decal_attachment_axes(axes,wallhit.normal,aligned));
    assert(std::fabs(aligned.up[1]+1.0f)<1.e-5f);
    decal_projector projector{};
    assert(make_sprite_decal_projector(
        aligned,{{0,-0.012f,0.015f}},{{0,0,0}},
        0.1f,0.1f,1,1,0,projector));
    auto const scan=project_cached_decal_world(scene,projector);
    assert(!scan.triangles.empty());
    return 0;
}
