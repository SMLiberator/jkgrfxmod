#include "renderer/sprite_decal_follow.hpp"
#include <cassert>
#include <cmath>

namespace {
bool close(float a,float b,float eps=1.e-4f) {
    return std::fabs(a-b)<=eps;
}
jkgm::gpu_3do_geometry triangle() {
    jkgm::gpu_3do_geometry g;
    jkgm::gpu_3do_vertex v{};
    v.x=12; v.y=24; v.z=36;
    // Projector coordinates must survive arbitrary receiver translations,
    // rotations, scale/mirroring and shear without being transformed.
    v.fax=0.15f; v.fay=0.40f; v.faz=0.70f;
    v.face_extra=0.65f;
    v.nx=0;v.ny=0;v.nz=1;
    v.lnx=0;v.lny=0;v.lnz=1;
    v.u=0.5f; v.v=63.5f;
    g.vertices.push_back(v);
    v.x=14;g.vertices.push_back(v);
    v.x=12;v.y=26;g.vertices.push_back(v);
    g.indices={0,1,2};
    jkgm::gpu_3do_group group{};
    group.offset=0;group.count=3;
    g.groups.push_back(group);
    return g;
}
void verify_round_trip(jkgm::gpu_3do_matrix34 const &m) {
    auto g=triangle();
    auto const original=g;
    assert(jkgm::sprite_decal_geometry_to_receiver_local(g,m));
    // The receiver transform reproduces world-space POSITION, while
    // projector UV/depth and angular fade remain in immutable decal space.
    for(size_t i=0;i<g.vertices.size();++i) {
        auto const &v=g.vertices[i];
        auto const &o=original.vertices[i];
        auto pos=jkgm::gpu_3do_native_transform_point(m,{{v.x,v.y,v.z}});
        assert(close(pos[0],o.x)&&close(pos[1],o.y)&&close(pos[2],o.z));
        assert(v.fax==o.fax&&v.fay==o.fay&&v.faz==o.faz);
        assert(v.face_extra==o.face_extra);
        assert(v.u==o.u&&v.v==o.v);
    }
    assert(g.groups.size()==1 && g.groups[0].count==3);
    for(auto const &v:g.vertices)
        for(int i=0;i<3;++i) {
            float const p= i==0?v.x:i==1?v.y:v.z;
            assert(p>=g.groups[0].bounds_min[i]-1.e-5f);
            assert(p<=g.groups[0].bounds_max[i]+1.e-5f);
        }
}
}

int main() {
    using jkgm::gpu_3do_matrix34;
    verify_round_trip({{1,0,0, 0,1,0, 0,0,1, 10,20,30}});
    verify_round_trip({{0,2,0, -3,0,0, 0,0,4, 4,5,6}});
    verify_round_trip({{-2,0,0, 0,1.5f,0, 0,0,0.5f, -2,10,8}});
    verify_round_trip({{1,0.2f,0, 0.5f,1,0, 0.1f,0.2f,2, 5,6,7}});
    verify_round_trip({{0.1f,0,0, 0,2,0, 0,0,4, 4,5,6}});

    // Degenerate R/L/U transforms are not invertible; preserve geometry.
    auto g=triangle();
    auto const before=g.vertices[0].x;
    assert(!jkgm::sprite_decal_geometry_to_receiver_local(
        g,{{0,0,0, 0,1,0, 0,0,1, 0,0,0}}));
    assert(g.vertices[0].x==before);

    // No pointer-only identity; Thing signature and model/mesh revision
    // must match before a cached decal is allowed to follow a receiver.
    jkgm::sprite_decal_receiver_identity a{123,100,200,3};
    assert(a.matches({123,100,200,3}));
    assert(!a.matches({123,100,200,4}));
    assert(!a.matches({123,101,200,3}));
    assert(!a.matches({124,100,200,3}));
    assert(!jkgm::sprite_decal_receiver_identity{}.matches({}));
    return 0;
}
