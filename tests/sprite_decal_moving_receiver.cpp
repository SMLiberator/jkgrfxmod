#include "renderer/sprite_decal_moving_receiver.hpp"
#include "renderer/sprite_decal_auto_align.hpp"
#include "renderer/sprite_decal_resource.hpp"
#include "renderer/sprite_decal_follow.hpp"
#include <cassert>
#include <cmath>

using namespace jkgm;
static bool near(float a,float b,float eps=0.0002f) {
    return std::fabs(a-b)<eps;
}
int main() {
    gpu_3do_instance inst{};
    inst.transform={{1,0,0, 0,1,0, 0,0,1, 3,4,5}};
    auto mesh=std::make_shared<gpu_3do_geometry>();
    auto vertex=[](float x,float y) {
        gpu_3do_vertex v{};
        v.x=x;v.y=y;v.z=0;
        v.nx=0;v.ny=0;v.nz=1;
        return v;
    };
    mesh->vertices={vertex(-1,-1),vertex(1,-1),vertex(1,1),vertex(-1,1)};
    mesh->indices={0,1,2, 0,2,3};
    gpu_3do_group group{};
    group.count=6;
    group.bounds_min={{-1,-1,0}};
    group.bounds_max={{1,1,0}};
    mesh->groups.push_back(group);
    inst.geometry=mesh;
    assert(sprite_decal_moving_receiver_near(inst,{{3,4,5.05f}}));
    assert(!sprite_decal_moving_receiver_near(inst,{{100,100,100}}));
    auto surfaces=sprite_decal_moving_receiver_world(inst);
    assert(surfaces.valid_world && surfaces.receivers.size()==2);
    assert(near(surfaces.receivers[0].normal[2],1));
    assert(near(surfaces.receivers[0].polygon[0][0],2));

    auto nearest=find_nearest_decal_surface(
        decal_world_geometry{},surfaces,{{3,4,5.05f}},{{0,0,-1}});
    assert(nearest.valid && near(nearest.normal[2],1));
    sprite_axes const axes{{{1,0,0}},{{0,1,0}},{{0,0,1}}};
    decal_projector projector{};
    assert(make_sprite_decal_projector(
        axes,{{3,4,5.05f}},{{0,0,0}},1,1,1,1,0,projector));
    auto scan=project_cached_decal_world(surfaces,projector,2048);
    assert(scan.valid_world && !scan.truncated && !scan.triangles.empty());
    sprite_decal_mesh output{};
    sprite_decal_texture_coords const uv{0.5f,63.5f,0.5f,63.5f};
    assert(make_sprite_decal_gpu_mesh(
        scan,projector,0x100000,uv,1.0f,output));
    assert(output.geometry);
    auto const first=output.geometry->vertices[0];
    assert(sprite_decal_geometry_to_receiver_local(*output.geometry,inst.transform));
    auto const &local=output.geometry->vertices[0];
    auto world=gpu_3do_native_transform_point(
        inst.transform,{{local.x,local.y,local.z}});
    assert(near(world[0],first.x));
    assert(near(world[1],first.y));
    assert(near(world[2],first.z));
    assert(local.u==first.u && local.v==first.v);

    // A translated receiver moves the existing decal without touching its
    // cached geometry, UVs or source sprite's position.
    auto moved=inst.transform;
    moved[9]+=10;
    moved[10]-=2;
    auto new_pos=gpu_3do_native_transform_point(
        moved,{{local.x,local.y,local.z}});
    assert(near(new_pos[0],first.x+10));
    assert(near(new_pos[1],first.y-2));
    assert(near(new_pos[2],first.z));

    // Nonuniform scaling and shearing still round-trip correctly on a
    // newly projected decal; the receiver's nodes may use arbitrary RLU.
    inst.transform={{2,0,0, 0.25f,1,0, 0,0,0.75f, 3,4,5}};
    auto scaled=sprite_decal_moving_receiver_world(inst);
    assert(scaled.receivers.size()==2);
    auto scan2=project_cached_decal_world(scaled,projector,2048);
    assert(!scan2.triangles.empty());
    sprite_decal_mesh output2{};
    assert(make_sprite_decal_gpu_mesh(
        scan2,projector,0x100000,uv,1.0f,output2));
    auto const first2=output2.geometry->vertices[0];
    assert(sprite_decal_geometry_to_receiver_local(
        *output2.geometry,inst.transform));
    auto const &loc2=output2.geometry->vertices[0];
    auto world2=gpu_3do_native_transform_point(
        inst.transform,{{loc2.x,loc2.y,loc2.z}});
    assert(near(world2[0],first2.x));
    assert(near(world2[1],first2.y));
    assert(near(world2[2],first2.z));
    return 0;
}
