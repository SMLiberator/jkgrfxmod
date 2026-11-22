#include "renderer/gpu_3do_preview.hpp"
#include "renderer/gpu_3do_frustum.hpp"
#include <cassert>
#include <cmath>
#include <limits>
#include <utility>
#include <iostream>

using namespace jkgm;
static gpu_3do_group group(float x,float y,float z,float extent=1.0f) {
    gpu_3do_group g{};
    g.count=3;
    g.bounds_min={{x-extent,y-extent,z-extent}};
    g.bounds_max={{x+extent,y+extent,z+extent}};
    return g;
}
static bool bounds(gpu_3do_geometry const &g,
                   std::array<float,3> &lo,std::array<float,3> &hi) {
    return g.cached_group_bounds(lo,hi);
}
int main() {
    std::array<float,3> lo{},hi{};
    gpu_3do_geometry empty;
    assert(!bounds(empty,lo,hi));
    gpu_3do_geometry mesh;
    mesh.groups.push_back(group(1,15,2));
    mesh.groups.push_back(group(4,15,2));
    mesh.groups.push_back(group(-2,15,2));
    assert(bounds(mesh,lo,hi));
    assert(lo[0]==-3 && hi[0]==5);
    assert(lo[1]==14 && hi[1]==16);
    // Reuse the same fully cached bound for any number of cameras.
    std::array<float,3> second_lo{},second_hi{};
    assert(bounds(mesh,second_lo,second_hi) &&
           second_lo==lo && second_hi==hi);
    auto const &opaque=mesh.cached_opaque_group_order();
    assert(opaque.size()==mesh.groups.size());
    assert(bounds(mesh,second_lo,second_hi));

    // Mutated COPY gets a fresh lazy cache, original remains unchanged.
    gpu_3do_geometry copied=mesh;
    copied.groups.push_back(group(250,15,2));
    assert(bounds(copied,second_lo,second_hi));
    assert(second_hi[0]==251);
    assert(bounds(mesh,second_lo,second_hi));
    assert(second_hi[0]==5);

    // Moves preserve already computed cache state.
    gpu_3do_geometry moved=std::move(copied);
    assert(bounds(moved,second_lo,second_hi));
    assert(second_hi[0]==251);

    // ANY malformed drawable group invalidates the entire union (fail open).
    gpu_3do_geometry bad;
    bad.groups.push_back(group(3,15,1));
    auto invalid=group(5,15,1);
    invalid.bounds_max[0]=std::numeric_limits<float>::quiet_NaN();
    bad.groups.push_back(invalid);
    assert(!bounds(bad,second_lo,second_hi));
    gpu_3do_geometry zero;
    auto unused=group(40,15,2);unused.count=0;
    zero.groups.push_back(unused);
    assert(!bounds(zero,second_lo,second_hi));

    // Rejection of a union is conservative for EACH group for the same view.
    gpu_3do_native_frustum frustum{};
    frustum.clip_far=1;frustum.near_plane=0.01f;frustum.far_plane=70.f;
    frustum.far_left=-1.f;frustum.right=1.f;
    frustum.far_top=1.f;frustum.bottom=-1.f;
    gpu_3do_matrix34 view{};
    view[0]=view[4]=view[8]=1.f;
    gpu_3do_geometry far;
    far.groups.push_back(group(500,15,2));
    far.groups.push_back(group(505,15,2));
    assert(bounds(far,lo,hi));
    assert(gpu_3do_batch_outside_frustum(frustum,view,lo,hi));
    for(auto const &g:far.groups)
        assert(gpu_3do_batch_outside_frustum(
            frustum,view,g.bounds_min,g.bounds_max));
    assert(bounds(mesh,lo,hi));
    assert(!gpu_3do_batch_outside_frustum(frustum,view,lo,hi));
    std::cout<<"Cross-camera immutable 3DO mesh bounds: OK\n";
}
