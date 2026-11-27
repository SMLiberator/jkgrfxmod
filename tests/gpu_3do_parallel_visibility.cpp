#include "renderer/gpu_3do_parallel_visibility.hpp"
#include "renderer/cpu_render_workers.hpp"
#include <algorithm>
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

    // Production scratch vectors are thread_local, so the worker lambda
    // must bind the submitter's storage explicitly, not name TLS directly.
    static thread_local std::vector<size_t> prepared_indices;
    static thread_local std::vector<gpu_3do_parallel_visibility> prepared_visibility;
    prepared_visibility.resize(instances.size());
    for(size_t i=0;i<instances.size();++i)
        if(i%5U!=0U)prepared_indices.push_back(i);
    for(int pass=0;pass<3;++pass) {
        for(auto &result:prepared_visibility)result.ready=false;
        std::array<size_t,cpu_render_workers::lanes> completed{};
        assert(workers.execute(
            [&,indices=&prepared_indices,visibility=&prepared_visibility]
            (unsigned lane) {
                size_t count=0U;
                for(size_t k=lane;k<indices->size();k+=cpu_render_workers::lanes) {
                    auto const i=(*indices)[k];
                    prepare_gpu_3do_parallel_visibility(
                        instances[i],identity(),f,true,(*visibility)[i]);
                    ++count;
                }
                completed[lane]=count;
            }));
        for(unsigned lane=0;lane<cpu_render_workers::lanes;++lane) {
            assert(completed[lane]>0U);
            assert(completed[lane]==(prepared_indices.size()+
                cpu_render_workers::lanes-1U-lane)/cpu_render_workers::lanes);
        }
        for(size_t i=0;i<instances.size();++i) {
            assert(prepared_visibility[i].ready==(i%5U!=0U));
            if(prepared_visibility[i].ready)
                assert(prepared_visibility[i].groups==expected.groups);
        }
    }

    // Repeated model layouts can correlate strongly with the original
    // modulo lane assignment. Weighting must distribute every item once,
    // and produce the same original-index results regardless of lane.
    std::array<size_t,cpu_render_workers::lanes> weights{},stride_weights{};
    std::array<std::vector<size_t>,cpu_render_workers::lanes> lists;
    for(size_t i=0;i<instances.size();++i) {
        size_t const cost=(i%3U==0U?100U:1U)+8U;
        lists[cpu_render_workers::weighted_lane(weights,cost)].push_back(i);
        stride_weights[i%cpu_render_workers::lanes]+=cost;
    }
    assert(*std::max_element(weights.begin(),weights.end())<
           *std::max_element(stride_weights.begin(),stride_weights.end()));
    std::vector<unsigned> visits(instances.size(),0U);
    for(auto const &list:lists)
        for(auto i:list)++visits[i];
    for(auto count:visits)assert(count==1U);
    for(auto &result:results)result.ready=false;
    assert(workers.execute([&](unsigned lane) {
        for(auto i:lists[lane])
            prepare_gpu_3do_parallel_visibility(instances[i],identity(),f,true,results[i]);
    }));
    for(auto const &result:results)
        assert(result.ready && result.groups==expected.groups);

    // Exercise every supported worker count, including the main lane,
    // repeated barriers, failure recovery and weighted assignment.
    for(unsigned count=1U;count<=cpu_render_workers::max_background_workers;++count) {
        cpu_render_workers variable(count);
        assert(variable.available());
        auto const active=variable.lane_count();
        assert(active==count+1U);
        for(int pass=0;pass<2;++pass) {
            for(auto &result:results)result.ready=false;
            std::array<size_t,cpu_render_workers::max_lanes> visited{};
            assert(variable.execute([&](unsigned lane) {
                for(size_t i=lane;i<instances.size();i+=active) {
                    prepare_gpu_3do_parallel_visibility(instances[i],identity(),f,true,results[i]);
                    ++visited[lane];
                }
            }));
            for(unsigned lane=0;lane<active;++lane)
                assert(visited[lane]==(instances.size()+active-1U-lane)/active);
            for(auto const &result:results)
                assert(result.ready && result.groups==expected.groups);
        }
        std::array<size_t,cpu_render_workers::max_lanes> variable_weights{};
        for(size_t i=0;i<instances.size();++i)
            assert(cpu_render_workers::weighted_lane(variable_weights,i%7U+1U,active)<active);
        assert(!variable.execute([&](unsigned lane) {
            if(lane==active-1U)throw std::runtime_error("render lane failure");
        }));
        assert(variable.execute([](unsigned){}));
    }

    // Sparse intersecting geometry must preserve exact original-index
    // flags AND the collector's opaque ordering in the compact list.
    auto sparse_geo=std::make_shared<gpu_3do_geometry>(*geo);
    sparse_geo->groups.clear();
    for(size_t i=0;i<32U;++i)
        sparse_geo->groups.push_back(geo->groups[i%4U]);
    auto sparse=base;
    sparse.geometry=sparse_geo;
    gpu_3do_parallel_visibility compact_result{},plain_result{};
    prepare_gpu_3do_parallel_visibility(sparse,identity(),f,true,plain_result,true,true);
    prepare_gpu_3do_parallel_visibility(sparse,identity(),f,true,compact_result,true,true,true);
    assert(compact_result.compact_ready && !compact_result.mesh_inside);
    assert(compact_result.groups==plain_result.groups);
    assert(compact_result.compact_frustum_rejected==8U);
    assert(compact_result.compact_backface_rejected==8U);
    std::vector<size_t> expected_visible;
    for(auto index:sparse_geo->cached_opaque_group_order())
        if(plain_result.groups[index]==gpu_3do_cull_reason::visible)
            expected_visible.push_back(index);
    assert(expected_visible.size()==16U);
    assert(compact_result.visible_opaque_groups==expected_visible);
    prepare_gpu_3do_parallel_visibility(sparse,identity(),f,true,compact_result,true,true,false);
    assert(!compact_result.compact_ready && compact_result.visible_opaque_groups.empty());

    // Acceptance must preserve every cull reason for contained and
    // intersecting meshes, including double-sided and back-facing groups.
    gpu_3do_parallel_visibility accepted{};
    prepare_gpu_3do_parallel_visibility(base,identity(),f,true,accepted,true);
    assert(!accepted.mesh_inside && accepted.groups==expected.groups);
    auto contained_geo=std::make_shared<gpu_3do_geometry>(*geo);
    contained_geo->groups[2].bounds_min=contained_geo->groups[0].bounds_min;
    contained_geo->groups[2].bounds_max=contained_geo->groups[0].bounds_max;
    auto contained=base;
    contained.geometry=contained_geo;
    gpu_3do_parallel_visibility legacy{};
    prepare_gpu_3do_parallel_visibility(contained,identity(),f,true,legacy);
    prepare_gpu_3do_parallel_visibility(contained,identity(),f,true,accepted,true);
    assert(accepted.mesh_inside && accepted.group_frustum_tests_avoided==4U);
    assert(accepted.groups==legacy.groups);
    prepare_gpu_3do_parallel_visibility(contained,identity(),f,false,accepted,true);
    assert(!accepted.mesh_inside && accepted.group_frustum_tests_avoided==0U);

    prepare_gpu_3do_parallel_visibility(base,identity(),f,true,accepted,true,true);
    assert(accepted.groups==expected.groups);
    prepare_gpu_3do_parallel_visibility(contained,identity(),f,true,accepted,true,true);
    assert(accepted.groups==legacy.groups);
    // Test the hot representation, grazing angles, multi-plane groups,
    // and the copy-reset cache used by modified/decal geometry.
    gpu_3do_geometry planes;
    planes.face_planes={{{{2,3,4}},{{1,2,-1}}},
                         {{{-2,0,1}},{{-1,0,0}}}};
    gpu_3do_group range{};
    range.plane_count=2U;
    assert(planes.cached_compact_planes().planes[0].d==4.0f);
    for(int x=-10;x<=10;++x)for(int y=-10;y<=10;++y) {
        std::array<float,3> eye{{float(x),float(y),float(x+2*y)}};
        auto const test=prepare_gpu_3do_face_plane_test(planes,eye,true);
        assert(test.planes);
        assert(gpu_3do_group_has_facing_plane(planes,range,eye,test)==
               gpu_3do_group_has_facing_plane(planes,range,eye,{}));
    }
    auto modified=planes;
    modified.face_planes[0].anchor[0]=12.0f;
    assert(modified.cached_compact_planes().planes[0].d==14.0f);
    assert(planes.cached_compact_planes().planes[0].d==4.0f);
    range.plane_count=1U;
    std::array<float,3> eye{{2,3,4}};
    auto const grazing=prepare_gpu_3do_face_plane_test(planes,eye,true);
    assert(!gpu_3do_group_has_facing_plane(planes,range,eye,grazing));
    eye[0]=std::nextafter(2.0f,3.0f);
    assert(gpu_3do_group_has_facing_plane(planes,range,eye,grazing));
    assert(!prepare_gpu_3do_face_plane_test(planes,
        {{std::numeric_limits<float>::max(),0,0}},true).planes);

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
