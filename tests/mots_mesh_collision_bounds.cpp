#include "renderer/mots_mesh_collision_bounds.hpp"
#include "renderer/mots_sphere_tree.hpp"
#include <cassert>
#include <cmath>
#include <limits>
#include <vector>

using namespace jkgm;

static bool near(float a,float b,float eps=0.001f) {
    return std::fabs(a-b)<eps;
}
static gpu_3do_matrix34 placement(float x,float y,float z) {
    return {{1,0,0, 0,1,0, 0,0,1, x,y,z}};
}

int main()
{
    // A long, narrow limb with a knee-origin mesh pivot.
    // Origin-centered 3DO radius is about 4; the fitted geometry sphere
    // has a center halfway down the leg and radius ~2.016.
    std::vector<thing_node_vector> limb{
        {{-0.25f,0,0}}, {{0.25f,0,0}},
        {{-0.25f,-4,0}}, {{0.25f,-4,0}}
    };
    mots_local_mesh_sphere fitted{};
    assert(mots_center_mesh_sphere(limb,fitted));
    float const expected_radius=std::sqrt(4.0f+0.25f*0.25f);
    assert(near(fitted.center[0],0));
    assert(near(fitted.center[1],-2));
    assert(near(fitted.center[2],0));
    assert(near(fitted.radius,expected_radius));
    assert(fitted.radius<4.0f);

    // Moving only the collision center must not move the actual model.
    mots_tree_node limb_node{};
    limb_node.has_mesh=true;
    limb_node.parent=-1;
    limb_node.center_on_geometry=true;
    limb_node.local_center=fitted.center;
    limb_node.mesh_radius=fitted.radius;
    limb_node.world_matrix=placement(10,20,30);
    thing_node_vector world_center{};
    float world_radius=0.0f;
    assert(mots_tree_world_sphere(limb_node,world_center,world_radius));
    assert(near(world_center[0],10));
    assert(near(world_center[1],18));
    assert(near(world_center[2],30));
    assert(near(world_radius,expected_radius));
    std::vector<mots_tree_node> nodes{limb_node};
    mots_tree_hit hit{};
    assert(mots_tree_sphere_cast(nodes,{{10,18,35}},{{0,0,-1}},
                                 10.0f,0.0f,hit));
    assert(near(hit.distance,5.0f-expected_radius));
    assert(near(hit.center[1],18.0f));

    // The original MotS mode still retains the authored 3DO Radius
    // and its historical 0.75 multiplier at the node origin.
    nodes[0].center_on_geometry=false;
    nodes[0].mesh_radius=4.0f;
    assert(mots_tree_world_sphere(nodes[0],world_center,world_radius));
    assert(near(world_center[1],20.0f) && near(world_radius,3.0f));

    // A rotation must carry the local sphere center with the joint.
    nodes[0]=limb_node;
    nodes[0].world_matrix={{0,1,0, -1,0,0, 0,0,1, 10,20,30}};
    assert(mots_tree_world_sphere(nodes[0],world_center,world_radius));
    assert(near(world_center[0],12.0f));
    assert(near(world_center[1],20.0f));
    assert(near(world_radius,expected_radius));

    // Nonuniform scaling is conservatively handled, not ignored.
    nodes[0].world_matrix={{1,0,0, 0,2,0, 0,0,1, 10,20,30}};
    assert(mots_tree_world_sphere(nodes[0],world_center,world_radius));
    assert(near(world_center[1],16.0f));
    assert(near(world_radius,2*expected_radius));

    // Broad-phase can be derived from the *current* pose, no treeSize
    // template field or direct SithThing layout change is necessary.
    float bound=0;
    assert(mots_tree_enclosing_radius(nodes,{{10,20,30}},bound));
    assert(bound>=4.0f+world_radius);
    nodes[0].world_matrix=placement(10,20,30);
    assert(mots_tree_enclosing_radius(nodes,{{10,20,30}},bound));
    assert(bound>=2.0f+fitted.radius);
    assert(bound<5.0f);

    // Invalid input must never change an existing valid sphere.
    mots_local_mesh_sphere unchanged=fitted;
    assert(!mots_center_mesh_sphere({},fitted));
    std::vector<thing_node_vector> invalid{{{NAN,0,0}}};
    assert(!mots_center_mesh_sphere(invalid,fitted));
    assert(fitted.center==unchanged.center &&
           fitted.radius==unchanged.radius);

    // A disconnected invalid hierarchy cannot produce a broad phase.
    nodes[0].parent=0;
    assert(!mots_tree_enclosing_radius(nodes,{{0,0,0}},bound));
}
