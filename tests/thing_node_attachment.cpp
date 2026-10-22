#include "renderer/thing_node_attachment.hpp"
#include <cassert>
#include <cmath>
using namespace jkgm;
static bool near(float a,float b){return std::fabs(a-b)<1e-5f;}
int main() {
    node_attachment_registry registry;
    node_attachment a{};
    a.child=0x101000U;a.parent=0x202000U;
    a.child_signature=123;a.parent_signature=456;
    a.node=4;
    a.offset={{0.5f,1.0f,-0.25f}};
    assert(node_attachment_valid(a));
    assert(registry.set(a));
    thing_node_pose node{},output{};
    node.origin={{5,6,7}};
    node.right={{-2,0,0}};
    node.look={{0,1,0}};
    node.up={{0,0,3}};
    assert(compute_node_attachment_pose(a,node,output));
    assert(near(output.origin[0],4)&&near(output.origin[1],7)&&
           near(output.origin[2],6.25f));
    assert(near(output.right[0],-1)&&near(output.up[2],1));
    assert(native_attach_node==0x80U);
    assert((native_attach_node&(native_attach_thing|native_attach_no_move))==0U);
    a.offset[0]=INFINITY;
    assert(!node_attachment_valid(a));
    a.offset[0]=0.5f;
    a.child=a.parent;assert(!node_attachment_valid(a));
    a.child=0x101000U;
    a.node=4096;assert(!node_attachment_valid(a));
    a.node=4;
    node_attachment b=a;
    b.child=0x202000U;b.parent=0x101000U;
    assert(!registry.set(b)); // would produce cycle
    b.parent=0x303000U;
    assert(registry.set(b));
    node_attachment c=b;
    c.child=0x303000U;c.parent=0x101000U;
    assert(!registry.set(c)); // transitive cycle
    registry.erase(a.child);
    assert(registry.snapshot().size()==1);
    registry.prepare_world(0x510000U);
    assert(registry.snapshot().size()==1);
    registry.prepare_world(0x510000U);
    assert(registry.snapshot().size()==1);
    registry.prepare_world(0x610000U);
    assert(registry.snapshot().empty());
    return 0;
}
