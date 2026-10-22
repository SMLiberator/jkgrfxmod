#include "renderer/spotlight.hpp"
#include <cassert>
#include <cmath>
using namespace jkgm;
static bool close(float a,float b) { return std::fabs(a-b)<0.0001f; }
int main()
{
    spotlight_registry reg;
    spotlight_request a{};
    a.thing=0x123456U;
    a.identity=7;
    a.node=-1;
    a.material=5;
    a.width=2.0f;a.height=1.2f;
    a.offset={{0.5f,1.0f,0.25f}};
    assert(spotlight_request_valid(a));
    assert(reg.set(a));
    auto s=reg.snapshot();
    assert(s.size()==1 && s[0].identity==7);
    thing_node_pose pose{};
    pose.right={{1,0,0}};
    pose.look={{0,1,0}};
    pose.up={{0,0,1}};
    pose.origin={{10,20,30}};
    spotlight_instance out{};
    assert(spotlight_make_instance(a,pose,out));
    assert(close(out.origin[0],10.5f) && close(out.origin[1],21.0f));
    assert(close(out.origin[2],30.25f));
    assert(close(out.forward[1],1) && close(out.range,8.0f));
    // Animated node movement changes the next light position/orientation.
    pose.origin={{12,22,32}};
    pose.look={{0,-1,0}};
    pose.right={{-1,0,0}};
    assert(spotlight_make_instance(a,pose,out));
    assert(close(out.origin[0],11.5f)&&close(out.origin[1],21.0f));
    assert(close(out.forward[1],-1.0f));
    // Mirror and nonuniform scale do not corrupt facing directions.
    pose.right={{-2,0,0}};
    assert(spotlight_make_instance(a,pose,out));
    assert(close(out.right[0],-1.0f));
    a.node=4096;assert(!spotlight_request_valid(a));
    a.node=0;a.width=0.0f;assert(!spotlight_request_valid(a));
    a.width=2.0f;
    a.material=0x10000U;assert(!spotlight_request_valid(a));
    a.material=5;
    reg.prepare_world(0x1111U);
    for(int n=0;n<3;n++) {
        a.node=n;
        assert(reg.set(a));
    }
    a.node=3;
    assert(!reg.set(a)); // four active lights maximum
    a.node=1;a.width=3.0f;
    assert(reg.set(a)); // replacing an existing node always works
    assert(reg.snapshot().size()==4);
    reg.prepare_world(0x1111U);
    assert(reg.snapshot().size()==4);
    reg.prepare_world(0x2222U);
    assert(reg.snapshot().empty());
    reg.set(a);
    reg.erase(a.thing,a.node);
    assert(reg.snapshot().empty());
    return 0;
}
