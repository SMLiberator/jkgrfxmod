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
    // Animated node-local RLU <-> absolute world XYZ uses the very same
    // affine conversion/inversion as GetThingOffsetPos/PosOffset.
    thing_node_vector node_local{{2,3,4}};
    thing_node_vector transformed{{99,99,99}},back{{99,99,99}};
    assert(get_thing_node_offset_pos(m,node_local,transformed));
    assert(close(transformed[0],7) && close(transformed[1],22) &&
           close(transformed[2],34));
    assert(get_thing_node_pos_offset(m,transformed,back));
    for(int i=0;i<3;++i)assert(close(back[i],node_local[i]));
    assert(get_thing_node_pos_offset(m,{{10,20,30}},back));
    for(float value:back)assert(close(value,0.0f));

    // Preserve nonuniform scale, shear and reflections, not just rotation.
    gpu_3do_matrix34 const skew{{
        -2,0,0, 1,3,0, 0,1,4, 10,20,30
    }};
    assert(get_thing_node_offset_pos(skew,{{2,3,4}},transformed));
    assert(close(transformed[0],9) && close(transformed[1],33) &&
           close(transformed[2],46));
    assert(get_thing_node_pos_offset(skew,transformed,back));
    assert(close(back[0],2) && close(back[1],3) && close(back[2],4));
    assert(get_thing_node_pos_offset(skew,{{9,33,46}},back));
    assert(close(back[0],2) && close(back[1],3) && close(back[2],4));

    // Forward mapping is defined for a singular basis; inverse must
    // reject it without exposing a partially transformed result.
    gpu_3do_matrix34 const singular{{
        0,0,0, 0,1,0, 0,0,1, 1,2,3
    }};
    assert(get_thing_node_offset_pos(singular,{{8,4,2}},transformed));
    assert(close(transformed[0],1) && close(transformed[1],6) &&
           close(transformed[2],5));
    back={{91,92,93}};
    assert(!get_thing_node_pos_offset(singular,transformed,back));
    assert(back[0]==91 && back[1]==92 && back[2]==93);
    assert(!get_thing_node_offset_pos(m,{{NAN,0,0}},transformed));
    assert(!get_thing_node_pos_offset(m,{{NAN,0,0}},back));
    auto invalid_node=m;
    invalid_node[4]=std::numeric_limits<float>::infinity();
    transformed={{81,82,83}};
    assert(!get_thing_node_offset_pos(invalid_node,node_local,transformed));
    assert(transformed[0]==81 && transformed[1]==82 &&
           transformed[2]==83);

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
