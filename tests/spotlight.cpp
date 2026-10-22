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
    a.material=5;
    a.width=2.0f;a.height=1.2f;a.range=12.0f;
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
    assert(close(out.forward[1],1) && close(out.range,12.0f));
    pose.origin={{12,22,32}};
    pose.look={{0,-1,0}};
    pose.right={{-1,0,0}};
    assert(spotlight_make_instance(a,pose,out));
    assert(close(out.origin[0],11.5f)&&close(out.origin[1],21.0f));
    assert(close(out.forward[1],-1.0f));
    pose.right={{-2,0,0}};
    assert(spotlight_make_instance(a,pose,out));
    assert(close(out.right[0],-1.0f));
    a.width=0.0f;assert(!spotlight_request_valid(a));
    a.width=2.0f;a.range=0.0f;assert(!spotlight_request_valid(a));
    a.range=-2.0f;assert(!spotlight_request_valid(a));
    a.range=12.0f;
    a.material=0x10000U;assert(!spotlight_request_valid(a));
    a.material=5;
    reg.prepare_world(0x1111U);
    a.width=3.0f;
    assert(reg.set(a)); // replace this Thing, don't append
    assert(reg.snapshot().size()==1);
    for(uintptr_t i=1;i<=3;i++) {
        spotlight_request other=a;
        other.thing=0x230000U+i;
        assert(reg.set(other));
    }
    spotlight_request fifth=a;
    fifth.thing=0x240000U;
    assert(!reg.set(fifth));
    assert(reg.snapshot().size()==4);
    reg.erase_thing(a.thing);
    assert(reg.snapshot().size()==3);
    assert(reg.set(fifth));
    reg.prepare_world(0x1111U);
    assert(reg.snapshot().size()==4);
    reg.prepare_world(0x2222U);
    assert(reg.snapshot().empty());

    // Default visual look and instantaneous intensity override.
    a.thing=0x123456U;
    a.identity=7;
    assert(reg.set(a));
    auto current=reg.snapshot().front();
    auto levels=spotlight_sample_intensities(current,1000U);
    assert(close(levels.light,1.0f) && close(levels.add,0.12f));
    assert(reg.set_intensities(a.thing,a.identity,{2.0f,0.5f},0.0f,1000U));
    current=reg.snapshot().front();
    levels=spotlight_sample_intensities(current,1000U);
    assert(close(levels.light,2.0f) && close(levels.add,0.5f));

    // Two-second fade and restart halfway through without a brightness jump.
    assert(reg.set_intensities(a.thing,a.identity,{4.0f,0.1f},2.0f,1000U));
    current=reg.snapshot().front();
    levels=spotlight_sample_intensities(current,2000U);
    assert(close(levels.light,3.0f) && close(levels.add,0.3f));
    assert(reg.set_intensities(a.thing,a.identity,{0.0f,1.0f},1.0f,2000U));
    current=reg.snapshot().front();
    levels=spotlight_sample_intensities(current,2500U);
    assert(close(levels.light,1.5f) && close(levels.add,0.65f));
    levels=spotlight_sample_intensities(current,3000U);
    assert(close(levels.light,0.0f) && close(levels.add,1.0f));
    levels=spotlight_sample_intensities(current,9000U);
    assert(close(levels.light,0.0f) && close(levels.add,1.0f));

    // Modifying the projector's MAT/size preserves its ongoing animation.
    assert(reg.set_intensities(a.thing,a.identity,{1.0f,0.0f},2.0f,10000U));
    a.material=9;
    assert(reg.set(a));
    current=reg.snapshot().front();
    assert(current.material==9U);
    levels=spotlight_sample_intensities(current,11000U);
    assert(close(levels.light,0.5f) && close(levels.add,0.5f));

    // Bad input and a recycled Thing signature cannot modify the light.
    assert(!reg.set_intensities(a.thing,999,{1.0f,1.0f},1.0f,11000U));
    assert(!reg.set_intensities(a.thing,a.identity,{-1.0f,1.0f},1.0f,11000U));
    assert(!reg.set_intensities(a.thing,a.identity,{1.0f,17.0f},1.0f,11000U));
    assert(!reg.set_intensities(a.thing,a.identity,{1.0f,1.0f},-1.0f,11000U));
    assert(!reg.set_intensities(a.thing,a.identity,{1.0f,1.0f},
                                INFINITY,11000U));

    // Unsigned GetTickCount arithmetic handles rollover during a fade.
    assert(reg.set_intensities(a.thing,a.identity,{3.0f,2.0f},
                               2.0f,0xffffff00U));
    current=reg.snapshot().front();
    levels=spotlight_sample_intensities(current,0x000002e8U); // +1000 ms
    assert(close(levels.light,2.0f) && close(levels.add,1.0f));

    // Recycled Thing pointer starts from defaults, not previous owner fade.
    a.identity=8;
    assert(reg.set(a));
    current=reg.snapshot().front();
    levels=spotlight_sample_intensities(current,11000U);
    assert(close(levels.light,1.0f) && close(levels.add,0.12f));
    assert(reg.snapshot().size()==1);
    reg.erase_thing(a.thing);
    assert(reg.snapshot().empty());
    return 0;
}
