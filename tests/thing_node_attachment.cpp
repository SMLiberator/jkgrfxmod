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
    assert(!native_attach_has_thing_parent(0));
    assert(!native_attach_has_thing_parent(0x1U|native_attach_no_move));
    assert(native_attach_has_thing_parent(native_attach_thing_face));
    assert(native_attach_has_thing_parent(native_attach_thing));
    assert(native_attach_has_thing_parent(0x8cU)); // Thing, NODE, NOMOVE
    assert(native_attach_has_thing_parent(0x82U)); // Thing-face, NODE
    assert(native_actor_death_confirmed(native_thing_actor,0.0f));
    assert(native_actor_death_confirmed(native_thing_corpse,0.0f));
    assert(!native_actor_death_confirmed(native_thing_actor,100.0f));
    assert(!native_actor_death_confirmed(native_thing_actor,NAN));
    assert(!native_actor_death_confirmed(1U,0.0f)); // players unchanged
    assert(!native_actor_death_confirmed(0U,0.0f)); // free Thing slots
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

    // Removing a parent returns only its node children, removes its own
    // attachment entry, and does not affect unrelated records.
    node_attachment_registry lifecycle;
    a.child=0x101000U;a.parent=0x202000U;
    assert(lifecycle.set(a));
    b.child=0x202000U;b.parent=0x303000U;
    assert(lifecycle.set(b));
    c.child=0x404000U;c.parent=0x202000U;
    assert(lifecycle.set(c));
    node_attachment other=c;
    other.child=0x505000U;other.parent=0x606000U;
    assert(lifecycle.set(other));
    auto descendants=lifecycle.take_related(0x202000U);
    assert(descendants.size()==2);
    assert(descendants[0].child==a.child);
    assert(descendants[1].child==c.child);
    auto remaining=lifecycle.snapshot();
    assert(remaining.size()==1 && remaining[0].child==other.child);
    assert(lifecycle.take_related(0x202000U).empty());
    assert(lifecycle.take_related(0x606000U).size()==1);
    assert(lifecycle.snapshot().empty());
    return 0;
}
