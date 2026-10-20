#include "renderer/thing_node_math.hpp"
#include <cassert>
#include <cmath>
#include <limits>
using namespace jkgm;
static bool close(float a,float b) {return std::fabs(a-b)<1e-4f;}
int main() {
    gpu_3do_matrix34 const m{{
        0,1,0, -1,0,0, 0,0,1, 10,20,30
    }};
    thing_node_pose pose{};
    assert(get_thing_node_pose(m,pose));
    assert(close(pose.origin[0],10));
    assert(close(pose.origin[1],20));
    assert(close(pose.origin[2],30));
    assert(close(pose.right[1],1) && close(pose.look[0],-1));
    thing_node_vector point{};
    assert(get_thing_node_point(m,{{2,3,4}},point));
    assert(close(point[0],7));
    assert(close(point[1],22));
    assert(close(point[2],34));
    // An attachment's local offset rotates with its parent node. Unlike
    // JK's problematic orientation-only attachment path, this composes
    // both rotation and translation.
    gpu_3do_matrix34 const local{{
        1,0,0, 0,1,0, 0,0,1, 2,3,4
    }};
    gpu_3do_matrix34 attached{};
    assert(get_thing_node_attachment(m,local,attached));
    assert(close(attached[9],7));
    assert(close(attached[10],22));
    assert(close(attached[11],34));
    // Native mirror must be preserved, not silently reoriented.
    auto mirrored=m;mirrored[0]=-1;
    assert(get_thing_node_pose(mirrored,pose));
    assert(close(pose.right[0],-1));
    auto nan_matrix=m;
    nan_matrix[3]=std::numeric_limits<float>::quiet_NaN();
    assert(!get_thing_node_pose(nan_matrix,pose));
    auto const old=attached;
    assert(!get_thing_node_attachment(nan_matrix,local,attached));
    assert(attached==old);
    assert(!get_thing_node_point(m,{{NAN,0,0}},point));
}
