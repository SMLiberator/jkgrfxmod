#include "renderer/gpu_3do_frustum.hpp"
#include <cassert>
int main() {
    jkgm::gpu_3do_native_frustum f{};
    f.clip_far=1;f.near_plane=1;f.far_plane=100;
    f.far_top=1;f.bottom=-1;f.far_left=-1;f.right=1;
    assert(!jkgm::gpu_3do_sphere_outside(f,true,0,10,0,1));
    assert(jkgm::gpu_3do_sphere_outside(f,true,0,-10,0,1));
    assert(jkgm::gpu_3do_sphere_outside(f,true,0,110,0,1));
    assert(jkgm::gpu_3do_sphere_outside(f,true,100,10,0,1));
    assert(jkgm::gpu_3do_sphere_outside(f,true,-100,10,0,1));
    assert(jkgm::gpu_3do_sphere_outside(f,true,0,10,100,1));
    assert(jkgm::gpu_3do_sphere_outside(f,true,0,10,-100,1));
    assert(!jkgm::gpu_3do_sphere_outside(f,true,10,10,0,1));
    assert(!jkgm::gpu_3do_sphere_outside(f,true,0,100,0,2));
    assert(!jkgm::gpu_3do_sphere_outside(f,true,0,110,0,20));
    // A large model can intersect the view while its *individual draw
    // batches* lie completely offscreen. Reject a batch only if all
    // corners are outside the same native frustum plane.
    jkgm::gpu_3do_matrix34 id{{1,0,0,0,1,0,0,0,1,0,0,0}};
    auto bounds_out=[&](std::array<float,3> lo,std::array<float,3> hi,
                        jkgm::gpu_3do_matrix34 const &m) {
        return jkgm::gpu_3do_batch_outside_frustum(f,m,lo,hi);
    };
    assert(!bounds_out({{-1,8,-1}},{{1,12,1}},id));
    assert(bounds_out({{20,8,-1}},{{24,12,1}},id)); // right
    assert(bounds_out({{-24,8,-1}},{{-20,12,1}},id)); // left
    assert(bounds_out({{-1,8,20}},{{1,12,24}},id)); // top
    assert(bounds_out({{-1,8,-24}},{{1,12,-20}},id)); // bottom
    assert(bounds_out({{-1,-10,-1}},{{1,-4,1}},id)); // behind
    assert(bounds_out({{-1,120,-1}},{{1,125,1}},id)); // beyond far
    assert(!bounds_out({{9,8,-1}},{{12,12,1}},id)); // partly visible
    auto moved=id; moved[9]=-20; // shift geometry left in view
    assert(bounds_out({{-1,8,-1}},{{1,12,1}},moved));
    auto mirror=id; mirror[0]=-1;
    assert(!bounds_out({{-1,8,-1}},{{1,12,1}},mirror));
    auto stretched=id;stretched[0]=25;
    assert(!bounds_out({{-1,8,-1}},{{1,12,1}},stretched));
    return 0;
}
