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
    auto scan=project_cached_decal_world(surfaces,projector,2048,false);
    assert(scan.valid_world && !scan.truncated && !scan.triangles.empty());
    // Moving nodes must not be displaced along each separate face normal:
    // those displacements separate otherwise shared triangle boundaries.
    for(auto const &tri:scan.triangles)
        for(auto const &v:tri.vertices)assert(near(v.world[2],5.0f));
    auto const static_bias=project_cached_decal_world(surfaces,projector,2048);
    assert(!static_bias.triangles.empty());
    assert(static_bias.triangles[0].vertices[0].world[2]>
           scan.triangles[0].vertices[0].world[2]);
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
    auto scan2=project_cached_decal_world(scaled,projector,2048,false);
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
    // A single projected SPR must cross a node boundary rather than
    // selecting whichever triangle/mesh lies closest to the impact.
    // Model two separately transformed mesh nodes that share a plane.
    gpu_3do_instance left{},right{};
    left.transform={{1,0,0, 0,1,0, 0,0,1, 0,0,0}};
    right.transform=left.transform;
    auto left_mesh=std::make_shared<gpu_3do_geometry>();
    auto right_mesh=std::make_shared<gpu_3do_geometry>();
    auto set_triangle=[&](std::shared_ptr<gpu_3do_geometry> const &m,
                          std::array<gpu_3do_vertex,3> const &v) {
        m->vertices.assign(v.begin(),v.end());
        m->indices={0,1,2};
        gpu_3do_group g{};
        g.count=3;
        for(int axis=0;axis<3;++axis) {
            g.bounds_min[axis]=100.0f;
            g.bounds_max[axis]=-100.0f;
        }
        for(auto const &vertex:v) {
            float const xyz[]={vertex.x,vertex.y,vertex.z};
            for(int axis=0;axis<3;++axis) {
                g.bounds_min[axis]=std::min(g.bounds_min[axis],xyz[axis]);
                g.bounds_max[axis]=std::max(g.bounds_max[axis],xyz[axis]);
            }
        }
        m->groups.push_back(g);
    };
    set_triangle(left_mesh,{{vertex(-1,-1),vertex(0,-1),vertex(0,1)}});
    set_triangle(right_mesh,{{vertex(0,-1),vertex(1,-1),vertex(0,1)}});
    left.geometry=left_mesh;right.geometry=right_mesh;
    decal_projector split{};
    assert(make_sprite_decal_projector(
        axes,{{0,0,0.05f}},{{0,0,0}},0.8f,0.8f,1,1,0,split));
    auto left_world=sprite_decal_moving_receiver_world(left);
    auto right_world=sprite_decal_moving_receiver_world(right);
    auto left_clip=project_cached_decal_world(left_world,split,2048,false);
    auto right_clip=project_cached_decal_world(right_world,split,2048,false);
    assert(!left_clip.triangles.empty());
    assert(!right_clip.triangles.empty());
    sprite_decal_mesh left_fragment{},right_fragment{};
    assert(make_sprite_decal_gpu_mesh(
        left_clip,split,0x100000,uv,1,left_fragment));
    assert(make_sprite_decal_gpu_mesh(
        right_clip,split,0x100000,uv,1,right_fragment));
    assert(sprite_decal_geometry_to_receiver_local(
        *left_fragment.geometry,left.transform));
    assert(sprite_decal_geometry_to_receiver_local(
        *right_fragment.geometry,right.transform));
    auto const lv=left_fragment.geometry->vertices[0];
    auto const rv=right_fragment.geometry->vertices[0];
    right.transform[9]+=3.0f;
    auto const changed_left=gpu_3do_native_transform_point(
        left.transform,{{lv.x,lv.y,lv.z}});
    auto const changed_right=gpu_3do_native_transform_point(
        right.transform,{{rv.x,rv.y,rv.z}});
    assert(near(changed_left[0],lv.x));
    assert(near(changed_right[0],rv.x+3.0f));
    // The bounding query must consider actual mesh geometry, including a
    // distant protruding node whose vertices sit beyond a Thing's ordinary
    // collision sphere.
    right.transform[9]=0;
    right.transform[9]=20;
    assert(sprite_decal_moving_receiver_near(right,{{20,0,0.05f}},1.0f));
    return 0;
}
