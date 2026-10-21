#include "renderer/mots_native_collision_query.hpp"
#include <cassert>
#include <cmath>
#include <limits>

using namespace jkgm;
static bool near(float a,float b) {return std::fabs(a-b)<0.0001f;}

int main()
{
    mots_native_model_snapshot model{};
    model.model=0x10080000;
    model.geoset=0;
    model.nodes.resize(3);
    model.nodes[0].hierarchy_index=0; // non-mesh root
    model.nodes[0].parent=-1;
    auto &a=model.nodes[1];
    a.hierarchy_index=1;
    a.parent=0;
    a.mesh_index=0;
    a.authored_radius=3.0f;
    a.local_vertices={{{0,0,0}},{{2,0,0}}};
    assert(mots_center_mesh_sphere(a.local_vertices,a.fitted_sphere));
    a.has_fitted_sphere=true;
    auto &b=model.nodes[2];
    b.hierarchy_index=2;
    b.parent=0;
    b.mesh_index=1;
    b.authored_radius=1.0f;
    b.local_vertices={{{0,0,0}},{{1,0,0}},{{0,1,0}}};
    assert(mots_center_mesh_sphere(b.local_vertices,b.fitted_sphere));
    b.has_fitted_sphere=true;

    int pose_calls=0;
    mots_native_pose_reader pose=[&](uint32_t index,gpu_3do_matrix34 &out) {
        ++pose_calls;
        float const x=index==1?10.0f:18.0f;
        out={{1,0,0, 0,1,0, 0,0,1, x,0,0}};
        return true;
    };
    mots_collision_query_hit hit{};
    mots_collision_query_stats stats{};
    thing_node_vector const origin{{0,0,0}},dir{{1,0,0}};
    thing_node_vector const thingpos{{10,0,0}};
    assert(mots_probe_collision_tree(
        model,pose,true,thingpos,origin,dir,25.0f,0.5f,hit,&stats)
        ==mots_query_status::hit);
    assert(hit.node==1 && near(hit.distance,9.5f));
    assert(near(hit.normal[0],-1.0f));
    assert(near(hit.sphere_center[0],11.0f));
    assert(stats.active_meshes==2 && stats.tested_broadphase &&
           !stats.rejected_broadphase && pose_calls==2);

    // Legacy origin-centered radius uses 3 * 0.75 = 2.25.
    assert(mots_probe_collision_tree(
        model,pose,false,thingpos,origin,dir,25.0f,0.5f,hit)
        ==mots_query_status::hit);
    assert(near(hit.distance,7.25f));
    assert(near(hit.sphere_center[0],10.0f));

    // A far-away trajectory is rejected conservatively by derived
    // broad phase without consulting any Thing.treeSize.
    mots_collision_query_hit previous=hit;
    assert(mots_probe_collision_tree(
        model,pose,true,thingpos,{{0,25,0}},dir,
        25.0f,0.5f,hit,&stats)==mots_query_status::miss);
    assert(stats.rejected_broadphase && hit.distance==previous.distance);
    // Passing close to an envelope (or tangent) must NOT be culled.
    assert(!mots_swept_sphere_definitely_outside(
        {{0,0,0}},dir,20,0.5f,{{10,2,0}},1.5f));
    assert(mots_swept_sphere_definitely_outside(
        {{0,0,0}},dir,20,0.5f,{{10,5,0}},1.5f));
    assert(!mots_swept_sphere_definitely_outside(
        {{0,0,0}},dir,20,0.5f,{{10,0,0}},
        std::numeric_limits<float>::quiet_NaN()));

    // Amputating node 1 must remove its sphere while retaining node 2.
    model.nodes[1].amputated=true;
    assert(mots_probe_collision_tree(
        model,pose,true,thingpos,origin,dir,25.0f,0.5f,hit)
        ==mots_query_status::hit);
    assert(hit.node==2);
    model.nodes[1].amputated=false;

    // When an animated pose is unreadable, it is NOT a collision miss.
    int fail_at=2;
    mots_native_pose_reader bad_pose=[&](uint32_t i,gpu_3do_matrix34 &out) {
        if(int(i)==fail_at)return false;
        return pose(i,out);
    };
    previous=hit;
    assert(mots_probe_collision_tree(
        model,bad_pose,true,thingpos,origin,dir,25,0.5f,hit)
        ==mots_query_status::unavailable);
    assert(hit.node==previous.node && hit.distance==previous.distance);
    // Input direction must be a normalized unit vector.
    assert(mots_probe_collision_tree(
        model,pose,true,thingpos,origin,{{2,0,0}},25,0.5f,hit)
        ==mots_query_status::unavailable);
    // No-hit must not clobber the last valid hit.
    assert(mots_probe_collision_tree(
        model,pose,true,thingpos,origin,dir,1,0.5f,hit)
        ==mots_query_status::miss);
    assert(hit.distance==previous.distance);

    // End-to-end dispatcher: checked Thing is type 2.
    mots_collision_participant const tree{2,4.0f};
    mots_collision_participant const sphere{1,1.0f};
    mots_collision_participant const face{3,0.5f};
    assert(mots_probe_type2_collision(
        &sphere,tree,nullptr,&model,pose,pose,{{0,0,0}},
        thingpos,origin,dir,25,0.5f,0,true,true,hit)
        ==mots_query_status::hit);
    assert(!hit.reverse_cast && hit.node==1 &&
           near(hit.distance,9.5f));

    // The MOVING Thing owns the tree: ray is reversed from the checked
    // Thing's position; the engine hit normal is negated afterwards.
    // Hide the second mesh for this particular assertion: otherwise the
    // checked Thing starts INSIDE that mesh sphere and its distance-zero
    // hit correctly wins over the first mesh.
    model.nodes[2].amputated=true;
    assert(mots_probe_type2_collision(
        &tree,sphere,&model,nullptr,pose,pose,thingpos,{{20,0,0}},
        origin,dir,25,0.5f,0,true,true,hit)
        ==mots_query_status::hit);
    assert(hit.reverse_cast && hit.node==1);
    assert(near(hit.distance,7.0f)); // start 20, center 11, radius 1+checked size 1
    assert(near(hit.normal[0],-1.0f));
    model.nodes[2].amputated=false;

    // Type 2 takes precedence over a face collider only when enabled.
    assert(mots_probe_type2_collision(
        &face,tree,nullptr,&model,pose,pose,{{0,0,0}},
        thingpos,origin,dir,25,0.5f,0,true,true,hit)
        ==mots_query_status::hit);
    assert(mots_probe_type2_collision(
        &tree,face,&model,nullptr,pose,pose,thingpos,{{20,0,0}},
        origin,dir,25,0.5f,0,true,true,hit)
        ==mots_query_status::hit);

    // Modes 1/3, disabled type2, RAYCAST_80, and unsupported flags all
    // remain unavailable to this adapter (native path must handle them).
    assert(mots_probe_type2_collision(
        &sphere,face,&model,&model,pose,pose,thingpos,thingpos,
        origin,dir,25,0.5f,0,true,true,hit)
        ==mots_query_status::unavailable);
    assert(mots_probe_type2_collision(
        &tree,sphere,&model,nullptr,pose,pose,thingpos,{{20,0,0}},
        origin,dir,25,0.5f,0,false,true,hit)
        ==mots_query_status::unavailable);
    assert(mots_probe_type2_collision(
        &tree,sphere,&model,nullptr,pose,pose,thingpos,{{20,0,0}},
        origin,dir,25,0.5f,mots_raycast_force_sphere,true,true,hit)
        ==mots_query_status::unavailable);
    assert(mots_probe_type2_collision(
        &tree,sphere,&model,nullptr,pose,pose,thingpos,{{20,0,0}},
        origin,dir,25,0.5f,0x400U,true,true,hit)
        ==mots_query_status::unavailable);
}
