#include "renderer/mots_sphere_tree.hpp"
#include <cassert>
#include <cmath>
#include <limits>
using namespace jkgm;
static bool close(float a,float b) {return std::fabs(a-b)<1e-4f;}
static gpu_3do_matrix34 make_pose(float x,float y,float z) {
    return {{1,0,0, 0,1,0, 0,0,1, x,y,z}};
}
int main() {
    std::vector<mots_tree_node> nodes(4);
    nodes[0].parent=-1; // root, no geometry
    nodes[1].parent=0;
    nodes[1].has_mesh=true;nodes[1].mesh_radius=2;
    nodes[1].world_matrix=make_pose(0,0,0);
    nodes[2].parent=0;
    nodes[2].has_mesh=true;nodes[2].mesh_radius=2;
    nodes[2].world_matrix=make_pose(10,0,0);
    nodes[3].parent=2;
    nodes[3].has_mesh=true;nodes[3].mesh_radius=2;
    nodes[3].world_matrix=make_pose(20,0,0);
    mots_tree_hit hit{};
    assert(mots_tree_sphere_cast(nodes,{{-6,0,0}},{{1,0,0}},
                                 30.0f,0.25f,hit));
    assert(hit.node==1);
    // Two radii combine: mesh radius 2 * MotS 0.75 + moving 0.25
    assert(close(hit.distance,4.25f));
    assert(close(hit.normal[0],-1));
    nodes[1].amputated=true;
    assert(mots_tree_sphere_cast(nodes,{{-6,0,0}},{{1,0,0}},
                                 30.0f,0.25f,hit));
    assert(hit.node==2);
    // Remove parent node 2: all its descendants must disappear too.
    nodes[2].amputated=true;
    assert(!mots_tree_sphere_cast(nodes,{{-6,0,0}},{{1,0,0}},
                                  30.0f,0.25f,hit));
    nodes[2].amputated=false;
    assert(!mots_tree_sphere_cast(nodes,{{-6,0,0}},{{1,0,0}},
                                  1.0f,0.25f,hit));
    assert(!mots_tree_sphere_cast(nodes,{{-6,0,0}},{{0,0,0}},
                                  30.0f,0.25f,hit));
    // Even if a node accidentally points to itself, traversal terminates.
    nodes[2].parent=2;
    assert(!mots_tree_node_active(nodes,2));
    assert(!mots_tree_node_active(nodes,3));
    assert(!mots_tree_sphere_cast(nodes,{{-6,0,0}},{{1,0,0}},
                                  30.0f,0.25f,hit));
    // Already inside a mesh sphere: return a distance-zero collision.
    nodes[2].parent=0;
    assert(mots_tree_sphere_cast(nodes,{{10,0,0}},{{1,0,0}},
                                 0,0,hit));
    assert(hit.node==2 && close(hit.distance,0));
    // The native sphere primitive uses a strict '< radius' comparison
    // against perpendicular distance, so an exact tangent is NOT a hit.
    nodes[2].parent=0;
    nodes[1].amputated=true;
    assert(!mots_tree_sphere_cast(nodes,{{6,1.5f,0}},{{1,0,0}},
                                  5.0f,0.0f,hit));
    // Exactly touching and moving away is not a collision. Moving
    // inward from that same boundary should report distance zero.
    assert(!mots_tree_sphere_cast(nodes,{{11.5f,0,0}},{{1,0,0}},
                                  5.0f,0.0f,hit));
    assert(mots_tree_sphere_cast(nodes,{{11.5f,0,0}},{{-1,0,0}},
                                 5.0f,0.0f,hit));
    assert(hit.node==2 && close(hit.distance,0.0f));
    assert(!mots_tree_sphere_cast(nodes,{{11.5f,0,0}},{{-1,0,0}},
                                  0.0f,0.0f,hit));
    // Bad parent references must not silently turn a node into a root.
    nodes[2].parent=-2;
    assert(!mots_tree_node_active(nodes,2));
    nodes[2].parent=0;
    // An enormous but finite separation formerly overflowed float dot
    // products: INF-INF produced NaN, making an invalid "hit" possible.
    std::vector<mots_tree_node> distant(1);
    distant[0].parent=-1;
    distant[0].has_mesh=true;
    distant[0].mesh_radius=1.0f;
    distant[0].world_matrix=make_pose(1.0e20f,0,0);
    mots_tree_hit untouched=hit;
    assert(!mots_tree_sphere_cast(distant,{{0,0,0}},{{-1,0,0}},
                                  10.0f,0.0f,hit));
    assert(hit.node==untouched.node && hit.distance==untouched.distance);
}
