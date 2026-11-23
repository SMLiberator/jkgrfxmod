#include "renderer/reflection_capture_parallel_visibility.hpp"
#include "renderer/cpu_render_workers.hpp"
#include <cassert>
#include <limits>
#include <memory>
#include <vector>

using namespace jkgm;
static gpu_3do_matrix34 identity() {
    gpu_3do_matrix34 m{};
    m[0]=m[4]=m[8]=1.0f;
    return m;
}
static reflection_capture_cull expected(
    gpu_3do_matrix34 const &modelview,
    gpu_3do_native_frustum const &f,
    bool hemi,float radius,gpu_3do_group const &g)
{
    if(hemi)
        return dual_paraboloid_bounds_outside(
            modelview,g.bounds_min,g.bounds_max,radius)
            ?reflection_capture_cull::hemisphere
            :reflection_capture_cull::visible;
    if(radius>0.0f &&
       reflection_probe_bounds_outside_range(
           modelview,g.bounds_min,g.bounds_max,radius))
        return reflection_capture_cull::range;
    if(gpu_3do_batch_outside_frustum(
        f,modelview,g.bounds_min,g.bounds_max))
        return reflection_capture_cull::frustum;
    return reflection_capture_cull::visible;
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
    geo->groups.resize(6);
    for(size_t i=0;i<geo->groups.size();++i) {
        auto &g=geo->groups[i];
        g.count=3U;
        float const x=float(i)*5.0f-10.0f;
        g.bounds_min={{x,-1.0f,-1.0f}};
        g.bounds_max={{x+1.0f,1.0f,1.0f}};
    }
    gpu_3do_instance inst{};
    inst.geometry=geo;
    inst.transform=identity();
    inst.transform[10]=10.0f;
    auto const view=identity();
    auto const modelview=gpu_3do_native_modelview(view,inst.transform);
    auto check=[&](bool hemi,float radius,
                   reflection_capture_parallel_visibility const &r) {
        assert(r.ready && r.modelview==modelview);
        std::array<float,3> lo{},hi{};
        assert(geo->cached_group_bounds(lo,hi));
        auto const whole=reflection_capture_bounds_reason(
            modelview,f,hemi,radius,lo,hi);
        assert(r.mesh==whole);
        if(whole!=reflection_capture_cull::visible) {
            assert(r.groups.empty());
        } else {
            assert(r.groups.size()==geo->groups.size());
            for(size_t i=0;i<geo->groups.size();++i)
                assert(r.groups[i]==expected(
                    modelview,f,hemi,radius,geo->groups[i]));
        }
    };
    // Test unchanged per-face ordering and all three visibility modes.
    cpu_render_workers workers;
    assert(workers.available());
    struct job {bool hemi;float radius;};
    std::vector<job> jobs{{false,0.0f},{false,3.0f},
                           {true,0.0f},{true,12.0f}};
    std::vector<reflection_capture_parallel_visibility> results(jobs.size());
    for(int pass=0;pass<4;++pass) {
        assert(workers.execute([&](unsigned lane) {
            for(size_t i=lane;i<jobs.size();i+=cpu_render_workers::lanes)
                prepare_reflection_capture_parallel_visibility(
                    inst,view,f,jobs[i].hemi,jobs[i].radius,results[i]);
        }));
        for(size_t i=0;i<jobs.size();++i)
            check(jobs[i].hemi,jobs[i].radius,results[i]);
    }
    // The serial route must produce identical results if workers fail.
    for(size_t i=0;i<jobs.size();++i) {
        reflection_capture_parallel_visibility serial{};
        prepare_reflection_capture_parallel_visibility(
            inst,view,f,jobs[i].hemi,jobs[i].radius,serial);
        assert(serial.ready==results[i].ready);
        assert(serial.mesh==results[i].mesh);
        assert(serial.modelview==results[i].modelview);
        assert(serial.groups==results[i].groups);
    }
    // The same authored face-plane logic used in the main-camera renderer
    // must hold for both serial and worker reflection visibility.
    {
        auto g=std::make_shared<gpu_3do_geometry>();
        g->groups.resize(3);
        g->face_planes.resize(2);
        for(auto &group:g->groups) {
            group.count=3U;
            group.bounds_min={{-0.25f,-0.25f,-0.25f}};
            group.bounds_max={{0.25f,0.25f,0.25f}};
        }
        g->face_planes[0].normal={{0.0f,-1.0f,0.0f}};
        g->face_planes[1].normal={{0.0f,1.0f,0.0f}};
        g->groups[0].plane_start=0U;g->groups[0].plane_count=1U;
        g->groups[1].plane_start=1U;g->groups[1].plane_count=1U;
        g->groups[2].plane_start=1U;g->groups[2].plane_count=1U;
        g->groups[2].face_type=1U; // double sided
        auto test_instance=inst;
        test_instance.geometry=g;
        reflection_capture_parallel_visibility serial{},parallel{};
        prepare_reflection_capture_parallel_visibility(
            test_instance,view,f,false,0.0f,serial);
        assert(serial.ready && serial.groups.size()==3U);
        assert(serial.groups[0]==reflection_capture_cull::visible);
        assert(serial.groups[1]==reflection_capture_cull::backface);
        assert(serial.groups[2]==reflection_capture_cull::visible);
        assert(workers.execute([&](unsigned lane) {
            if(lane==0U)prepare_reflection_capture_parallel_visibility(
                test_instance,view,f,false,0.0f,parallel);
        }));
        assert(parallel.ready && parallel.groups==serial.groups);
    }
    assert(!workers.execute([](unsigned lane) {
        if(lane==1U)throw 1;
    }));
    assert(workers.execute([](unsigned){}));
    auto missing=inst;
    missing.geometry.reset();
    prepare_reflection_capture_parallel_visibility(
        missing,view,f,false,0.0f,results[0]);
    assert(!results[0].ready);
    auto invalid=inst;
    invalid.transform[0]=std::numeric_limits<float>::quiet_NaN();
    prepare_reflection_capture_parallel_visibility(
        invalid,view,f,false,0.0f,results[0]);
    assert(!results[0].ready);
    return 0;
}
