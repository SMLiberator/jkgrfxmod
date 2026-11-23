#include "renderer/gpu_3do_parallel_visibility.hpp"
#include "renderer/cpu_render_workers.hpp"
#include <array>
#include <cassert>
#include <memory>
#include <stdexcept>
#include <vector>

using namespace jkgm;
static gpu_3do_matrix34 identity() {
    gpu_3do_matrix34 m{};
    m[0]=m[4]=m[8]=1.0f;
    return m;
}
int main() {
    gpu_3do_native_frustum f{};
    f.near_plane=0.1f;
    f.far_plane=100.0f;
    f.clip_far=1;
    f.far_left=-1.0f;
    f.right=1.0f;
    f.far_top=1.0f;
    f.bottom=-1.0f;
    auto geo=std::make_shared<gpu_3do_geometry>();
    geo->groups.resize(4);
    geo->face_planes.resize(4);
    for(size_t i=0;i<4;++i) {
        auto &g=geo->groups[i];
        // cached_group_bounds intentionally ignores groups with no indices.
        // Give the synthetic groups real drawable ranges, as live 3DOs do.
        g.count=3U;
        g.bounds_min={{-1.0f,-1.0f,-1.0f}};
        g.bounds_max={{1.0f,1.0f,1.0f}};
        g.plane_start=uint32_t(i);
        g.plane_count=1;
        geo->face_planes[i].anchor={{0.0f,0.0f,0.0f}};
        geo->face_planes[i].normal={{0.0f,-1.0f,0.0f}};
    }
    geo->face_planes[1].normal={{0.0f,1.0f,0.0f}};
    geo->groups[2].bounds_min={{100.0f,-1.0f,-1.0f}};
    geo->groups[2].bounds_max={{101.0f,1.0f,1.0f}};
    geo->groups[3].face_type=1U; // double-sided: never backface reject
    geo->face_planes[3].normal={{0.0f,1.0f,0.0f}};

    gpu_3do_instance base{};
    base.geometry=geo;
    base.transform=identity();
    base.transform[10]=10.0f; // in front of camera along native +Y
    gpu_3do_parallel_visibility expected{};
    prepare_gpu_3do_parallel_visibility(base,identity(),f,true,expected);
    assert(expected.ready && !expected.mesh_outside);
    assert(expected.has_local_eye && !expected.reversed_winding);
    assert(expected.groups.size()==4U);
    assert(expected.groups[0]==gpu_3do_cull_reason::visible);
    assert(expected.groups[1]==gpu_3do_cull_reason::backface);
    assert(expected.groups[2]==gpu_3do_cull_reason::frustum);
    assert(expected.groups[3]==gpu_3do_cull_reason::visible);

    // Compare each worker's independently computed result with the serial
    // implementation, indexed by ORIGINAL group number regardless of order.
    cpu_render_workers workers;
    assert(workers.available());
    std::vector<gpu_3do_instance> instances(72,base);
    std::vector<gpu_3do_parallel_visibility> results(instances.size());
    for(int pass=0;pass<3;++pass) {
        bool const ok=workers.execute([&](unsigned lane) {
            for(size_t i=lane;i<instances.size();i+=cpu_render_workers::lanes)
                prepare_gpu_3do_parallel_visibility(
                    instances[i],identity(),f,true,results[i]);
        });
        assert(ok);
        for(auto const &x:results) {
            assert(x.ready && !x.mesh_outside);
            assert(x.modelview==expected.modelview);
            assert(x.local_eye==expected.local_eye);
            assert(x.has_local_eye==expected.has_local_eye);
            assert(x.reversed_winding==expected.reversed_winding);
            assert(x.groups==expected.groups);
        }
    }

    // A worker exception must finish the barrier and permit safe serial
    // fallback. Subsequent dispatches should still be usable.
    assert(!workers.execute([](unsigned lane) {
        if(lane==0U)throw std::runtime_error("test worker error");
    }));
    assert(workers.execute([](unsigned) {}));

    gpu_3do_parallel_visibility offscreen{};
    auto far_geo=std::make_shared<gpu_3do_geometry>();
    far_geo->groups.push_back(geo->groups[2]);
    gpu_3do_instance far_inst=base;
    far_inst.geometry=far_geo;
    std::array<float,3> far_min{},far_max{};
    assert(far_geo->groups[0].count>0U);
    assert(far_geo->cached_group_bounds(far_min,far_max));
    assert(far_min[0]==100.0f && far_max[0]==101.0f);
    prepare_gpu_3do_parallel_visibility(far_inst,identity(),f,true,offscreen);
    assert(offscreen.ready && offscreen.mesh_outside);

    // Unavailable geometry must fail open to the old serial draw collector.
    gpu_3do_instance missing=base;
    missing.geometry.reset();
    prepare_gpu_3do_parallel_visibility(missing,identity(),f,true,offscreen);
    assert(!offscreen.ready);
    return 0;
}
