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
    assert(reg.erase_thing(a.thing));
    assert(!reg.erase_thing(a.thing)); // idempotent; no stale projector
    assert(reg.snapshot().empty());
    assert(reg.set(a));
    assert(reg.erase_thing(a.thing));
    assert(reg.snapshot().empty());

    // Conservative screen-space bounds for the fullscreen spotlight pass.
    spotlight_projection_camera cam{};
    cam.position={{0,0,0}};
    cam.right={{1,0,0}};
    cam.forward={{0,1,0}};
    cam.up={{0,0,1}};
    cam.tan_half_horizontal=1.0f;
    cam.tan_half_vertical=0.75f;
    cam.width=800;cam.height=600;
    spotlight_instance beam{};
    beam.origin={{0,4,0}};
    beam.right={{1,0,0}};
    beam.forward={{0,1,0}};
    beam.up={{0,0,1}};
    beam.width=3;beam.height=2;beam.range=12;
    auto rect=spotlight_screen_coverage(beam,cam);
    assert(rect.visible);
    assert(rect.min_x<0.5f&&rect.max_x>0.5f);
    assert(rect.min_y<0.5f&&rect.max_y>0.5f);
    assert(rect.min_x>0.0f&&rect.max_x<1.0f);
    // Exhaustively sample the cone interior: every screen-visible receiver
    // inside the projector must lie within the rectangle (no light clipping).
    auto assert_contains=[&](spotlight_instance const &light) {
        auto const bounds=spotlight_screen_coverage(light,cam);
        for(int n=1;n<=20;n++)for(int x=-10;x<=10;x++)
            for(int z=-10;z<=10;z++) {
                float const t=float(n)/20.0f;
                thing_node_vector p{};
                for(int axis=0;axis<3;axis++)
                    p[axis]=light.origin[axis]+
                        t*(light.forward[axis]*light.range+
                           light.right[axis]*light.width*float(x)/20.0f+
                           light.up[axis]*light.height*float(z)/20.0f);
                thing_node_vector d{{p[0]-cam.position[0],
                                     p[1]-cam.position[1],
                                     p[2]-cam.position[2]}};
                float const forward=d[0]*cam.forward[0]+d[1]*cam.forward[1]+
                                    d[2]*cam.forward[2];
                if(forward<=0.001f)continue;
                float const u=0.5f+0.5f*
                    (d[0]*cam.right[0]+d[1]*cam.right[1]+d[2]*cam.right[2])/
                    (forward*cam.tan_half_horizontal);
                float const v=0.5f+0.5f*
                    (d[0]*cam.up[0]+d[1]*cam.up[1]+d[2]*cam.up[2])/
                    (forward*cam.tan_half_vertical);
                if(u<0.0f||u>1.0f||v<0.0f||v>1.0f)continue;
                assert(bounds.visible);
                assert(u>=bounds.min_x-0.0001f && u<=bounds.max_x+0.0001f);
                assert(v>=bounds.min_y-0.0001f && v<=bounds.max_y+0.0001f);
            }
    };
    assert_contains(beam);
    // A camera partly inside the projector requires near-plane edge clipping.
    cam.position={{0,8,0}};
    assert_contains(beam);
    cam.position={{0,0,0}};
    beam.origin={{50,4,0}};
    assert(!spotlight_screen_coverage(beam,cam).visible);
    beam.origin={{0,-4,0}};
    beam.forward={{0,-1,0}};
    assert(!spotlight_screen_coverage(beam,cam).visible);
    cam.tan_half_horizontal=0.0f;
    assert(spotlight_screen_coverage(beam,cam).visible); // safe fallback
    return 0;
}
