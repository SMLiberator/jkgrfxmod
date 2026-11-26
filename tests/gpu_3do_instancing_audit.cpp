#include "renderer/gpu_3do_instancing_audit.hpp"
#include "renderer/frame_profile.hpp"
#include <cassert>
#include <limits>
#include <memory>
#include <vector>

using namespace jkgm;

static gpu_3do_instancing_audit_record record(
    gpu_3do_instance const &inst,gpu_3do_group const &group,
    uintptr_t material,uintptr_t owner,bool view_dependent=false,
    gpu_3do_alpha_mode policy=gpu_3do_alpha_mode::solid)
{
    gpu_3do_instancing_audit_record r{};
    r.mesh_id=inst.id;
    r.geometry=reinterpret_cast<uintptr_t>(inst.geometry.get());
    r.group=reinterpret_cast<uintptr_t>(&group);
    r.material=material;
    r.instance=owner;
    r.policy=policy;
    bool const valid_state=
        gpu_3do_instancing_capture_state(inst,false,r.state);
    r.state_eligible=gpu_3do_instancing_conservative_candidate(
        inst,group,policy,view_dependent) && valid_state;
    r.rejection_mask=gpu_3do_instancing_rejection_mask(
        inst,group,policy,
        view_dependent?gpu_3do_instancing_rejection_bit(
            gpu_3do_instancing_rejection::specular):0U,valid_state);
    assert((r.rejection_mask==0U)==r.state_eligible);
    return r;
}
int main() {
    {
        using R=gpu_3do_instancing_rejection;
        auto bit=gpu_3do_instancing_rejection_bit;
        uint32_t const shared=bit(R::specular)|bit(R::normal_map)|
            bit(R::surface_map)|bit(R::environment_map)|
            bit(R::matcap)|bit(R::clearcoat);
        assert(gpu_3do_instancing_material_policy(shared,false,0)==shared);
        assert(gpu_3do_instancing_material_policy(shared,true,0)==0U);
        assert(gpu_3do_instancing_material_policy(shared,true,7)==0U);
        for(int mode:{4,5,6})
            assert(gpu_3do_instancing_material_policy(shared,true,mode)==
                   bit(R::local_probe));
        assert(gpu_3do_instancing_material_policy(
            shared|bit(R::displacement),true,7)==bit(R::displacement));
        assert(gpu_3do_instancing_material_policy(
            shared|bit(R::per_thing_lights),true,0)==bit(R::per_thing_lights));
    }
    {
        std::array<float,12> const matrix{{1,2,3,4,5,6,7,8,9,10,11,12}};
        auto const rows=gpu_3do_instance_transform_texels(matrix);
        for(size_t row=0;row<3U;++row)
            for(size_t col=0;col<4U;++col)
                assert(rows[row][col]==matrix[col*3U+row]);
        assert(gpu_3do_instance_buffer_can_fit(10U,5U,35U));
        assert(!gpu_3do_instance_buffer_can_fit(10U,5U,34U));
        assert(!gpu_3do_instance_buffer_can_fit(10U,0U,29U));
        assert(!gpu_3do_instance_buffer_can_fit(16777217U,0U,100000000U));
    }
    // Filename is purposefully absent from the identity key. CloneModel can
    // retain "gen_empty.3do" while every mesh/material is independently edited.
    char const *filename="gen_empty.3do";
    assert(filename[0]=='g');
    auto shared=std::make_shared<gpu_3do_geometry>();
    shared->groups.emplace_back();
    gpu_3do_instance a{},b{};
    a.id=b.id=71U;
    a.geometry=b.geometry=shared;
    assert(gpu_3do_instancing_conservative_candidate(
        a,shared->groups[0],gpu_3do_alpha_mode::solid,false));
    auto rows=std::vector<gpu_3do_instancing_audit_record>{
        record(a,shared->groups[0],0x1000U,0x11U),
        record(b,shared->groups[0],0x1000U,0x22U)};
    auto metrics=audit_gpu_3do_instancing(rows);
    assert(metrics.source_draws==2U);
    assert(metrics.geometry_repeated_draws==1U);
    assert(metrics.compatible_repeated_draws==1U);
    assert(metrics.compatible_groups==1U);

    // Alpha masking stays in the opaque depth-writing pass, and permits
    // instancing of identical geometry, preserving shader cutoff behavior.
    assert(gpu_3do_instancing_opaque_policy(gpu_3do_alpha_mode::solid));
    assert(gpu_3do_instancing_opaque_policy(gpu_3do_alpha_mode::masked));
    assert(gpu_3do_draws_in_pass(gpu_3do_alpha_mode::masked,false));
    assert(!gpu_3do_draws_in_pass(gpu_3do_alpha_mode::masked,true));
    assert(gpu_3do_fragment_visible(
        gpu_3do_alpha_mode::masked,false,0.6f,0.5f));
    assert(!gpu_3do_fragment_visible(
        gpu_3do_alpha_mode::masked,false,0.4f,0.5f));
    for(auto policy:{gpu_3do_alpha_mode::mixed,
                     gpu_3do_alpha_mode::blend_only,
                     gpu_3do_alpha_mode::dithered}) {
        auto const rejected=record(
            a,shared->groups[0],0x1000U,0x11U,false,policy);
        assert(!gpu_3do_instancing_opaque_policy(policy));
        assert(!rejected.state_eligible);
        assert((rejected.rejection_mask&
            gpu_3do_instancing_rejection_bit(
                gpu_3do_instancing_rejection::alpha_policy))!=0U);
    }
    auto const masked_first=record(
        a,shared->groups[0],0x1000U,0x11U,false,
        gpu_3do_alpha_mode::masked);
    auto const masked_second=record(
        b,shared->groups[0],0x1000U,0x22U,false,
        gpu_3do_alpha_mode::masked);
    assert(masked_first.state_eligible && masked_second.state_eligible);
    rows={masked_first,masked_second};
    metrics=audit_gpu_3do_instancing(rows);
    assert(metrics.compatible_repeated_draws==1U);
    assert(metrics.alpha_policy_ranges[
        static_cast<size_t>(gpu_3do_alpha_mode::masked)]==2U);
    auto masked_schedule=schedule_gpu_3do_instancing(rows);
    assert(masked_schedule.size()==2U);
    assert(masked_schedule[0].batch_id!=0U);
    assert(masked_schedule[1].batch_id==masked_schedule[0].batch_id);
    // Both are eligible independently, but must never share a draw:
    // fragment alpha cutoff is a draw-wide shader uniform.
    rows={record(a,shared->groups[0],0x1000U,0x11U,false,
                 gpu_3do_alpha_mode::solid),masked_second};
    metrics=audit_gpu_3do_instancing(rows);
    assert(metrics.geometry_repeated_draws==1U);
    assert(metrics.conservative_draws==2U);
    assert(metrics.compatible_repeated_draws==0U);
    masked_schedule=schedule_gpu_3do_instancing(rows);
    assert(masked_schedule.size()==2U);
    assert(masked_schedule[0].batch_id==0U);
    assert(masked_schedule[1].batch_id==0U);
    // JK's translucent face bit remains an independent blocker.
    gpu_3do_group translucent_face=shared->groups[0];
    translucent_face.face_type=0x2U;
    assert(!gpu_3do_instancing_conservative_candidate(
        a,translucent_face,gpu_3do_alpha_mode::masked,false));
    assert(!gpu_3do_instancing_conservative_candidate(
        a,shared->groups[0],gpu_3do_alpha_mode::masked,true));
    rows={record(a,shared->groups[0],0x1000U,0x11U,false,
                 gpu_3do_alpha_mode::solid),
          record(a,shared->groups[0],0x1000U,0x12U,false,
                 gpu_3do_alpha_mode::mixed),
          masked_first,
          record(a,shared->groups[0],0x1000U,0x13U,false,
                 gpu_3do_alpha_mode::blend_only),
          record(a,shared->groups[0],0x1000U,0x14U,false,
                 gpu_3do_alpha_mode::dithered)};
    metrics=audit_gpu_3do_instancing(rows);
    assert(metrics.source_draws==5U);
    for(auto count:metrics.alpha_policy_ranges)assert(count==1U);
    rows={record(a,shared->groups[0],0x1000U,0x11U),
          record(b,shared->groups[0],0x1000U,0x22U)};

    // Same filename, different immutable snapshot ID: never merge.
    auto clone=std::make_shared<gpu_3do_geometry>();
    clone->groups.emplace_back();
    gpu_3do_instance changed=a;
    changed.id=72U;
    changed.geometry=clone;
    rows.push_back(record(changed,clone->groups[0],0x1000U,0x33U));
    metrics=audit_gpu_3do_instancing(rows);
    assert(metrics.source_draws==3U);
    assert(metrics.geometry_repeated_draws==1U);

    // Even if an ID is reused incorrectly, the actual geometry pointer
    // and group pointer are independently required.
    changed.id=71U;
    rows.push_back(record(changed,clone->groups[0],0x1000U,0x44U));
    metrics=audit_gpu_3do_instancing(rows);
    assert(metrics.geometry_repeated_draws==1U);

    // Material must resolve to the same current frame cache object.
    rows.push_back(record(b,shared->groups[0],0x2000U,0x55U));
    metrics=audit_gpu_3do_instancing(rows);
    assert(metrics.geometry_repeated_draws==1U);

    // Identical geometry but different per-Thing ambient: broad repeat,
    // not compatible instancing. Same for CMP, tint and mirrored winding.
    auto ambient=b;
    ambient.ambient=0.5f;
    rows={record(a,shared->groups[0],0x1000U,0x11U),
          record(ambient,shared->groups[0],0x1000U,0x22U)};
    metrics=audit_gpu_3do_instancing(rows);
    assert(metrics.geometry_repeated_draws==1U);
    assert(metrics.compatible_repeated_draws==0U);
    ambient=a;
    ambient.colormap_tint[0]=0.75f;
    auto cmp=record(ambient,shared->groups[0],0x1000U,0x22U);
    rows[1]=cmp;
    assert(audit_gpu_3do_instancing(rows).compatible_repeated_draws==0U);
    ambient=a;
    ambient.sector_tint[1]=0.2f;
    rows[1]=record(ambient,shared->groups[0],0x1000U,0x22U);
    assert(audit_gpu_3do_instancing(rows).compatible_repeated_draws==0U);
    rows[1]=record(a,shared->groups[0],0x1000U,0x22U);
    std::array<uint32_t,11> mirrored{};
    assert(gpu_3do_instancing_capture_state(a,true,mirrored));
    rows[1].state=mirrored;
    assert(audit_gpu_3do_instancing(rows).compatible_repeated_draws==0U);

    // Matching geometry is insufficient for alpha sorting, reflection
    // matrices, per-Thing point lights, active COG fades or 3DO mutations.
    assert(!gpu_3do_instancing_conservative_candidate(
        a,shared->groups[0],gpu_3do_alpha_mode::mixed,false));
    assert(!gpu_3do_instancing_conservative_candidate(
        a,shared->groups[0],gpu_3do_alpha_mode::solid,true));
    ambient=a;ambient.num_lights=1;
    assert(!gpu_3do_instancing_conservative_candidate(
        ambient,shared->groups[0],gpu_3do_alpha_mode::solid,false));
    ambient=a;ambient.thing_opacity=0.5f;
    assert(!gpu_3do_instancing_conservative_candidate(
        ambient,shared->groups[0],gpu_3do_alpha_mode::solid,false));
    ambient=a;ambient.is_sprite_quad=true;
    assert(!gpu_3do_instancing_conservative_candidate(
        ambient,shared->groups[0],gpu_3do_alpha_mode::solid,false));
    ambient=a;ambient.ambient=(std::numeric_limits<float>::quiet_NaN)();
    assert(!gpu_3do_instancing_capture_state(ambient,false,mirrored));
    // A single pending draw may fail several independent conditions.
    using Reason=gpu_3do_instancing_rejection;
    auto excluded=a;
    excluded.num_lights=1U;
    excluded.crossing_lighting=
        std::make_shared<gpu_3do_crossing_state>();
    excluded.thing_opacity=0.5f;
    auto const mask=gpu_3do_instancing_rejection_mask(
        excluded,shared->groups[0],gpu_3do_alpha_mode::mixed,
        gpu_3do_instancing_rejection_bit(Reason::normal_map) |
        gpu_3do_instancing_rejection_bit(Reason::environment_map),true);
    for(auto reason:{Reason::alpha_policy,Reason::normal_map,
                     Reason::environment_map,Reason::opacity,
                     Reason::per_thing_lights,Reason::crossing_lighting})
        assert((mask&gpu_3do_instancing_rejection_bit(reason))!=0U);
    assert(gpu_3do_instancing_rejection_mask(
        a,shared->groups[0],gpu_3do_alpha_mode::solid,0U,true)==0U);
    assert(gpu_3do_instancing_rejection_mask(
        a,shared->groups[0],gpu_3do_alpha_mode::solid,0U,false)
        ==gpu_3do_instancing_rejection_bit(Reason::invalid_state));
    auto first=record(a,shared->groups[0],0x1000U,0x11U);
    auto second=record(excluded,shared->groups[0],0x1000U,0x22U);
    second.rejection_mask=mask;
    second.state_eligible=false;
    rows={first,second};
    metrics=audit_gpu_3do_instancing(rows);
    assert(metrics.source_draws==2U);
    assert(metrics.geometry_repeated_draws==1U);
    assert(metrics.repeated_member_ranges==2U);
    assert(metrics.repeated_eligible_member_ranges==1U);
    assert(metrics.rejected_ranges==1U);
    assert(metrics.repeated_rejected_ranges==1U);
    assert(metrics.rejects_all[static_cast<size_t>(Reason::normal_map)]==1U);
    assert(metrics.rejects_repeated[static_cast<size_t>(Reason::normal_map)]==1U);
    assert(metrics.rejects_repeated[static_cast<size_t>(Reason::per_thing_lights)]==1U);
    assert(metrics.eligible_repeat_extras==0U);
    assert(metrics.state_split_extras==0U);

    // Identical eligible geometry can still be split by per-Thing ambient
    // state, while repeated ranges from the same Thing are not instances.
    auto differing=b;
    differing.ambient=0.5f;
    rows={first,record(differing,shared->groups[0],0x1000U,0x22U)};
    metrics=audit_gpu_3do_instancing(rows);
    assert(metrics.repeated_eligible_member_ranges==2U);
    assert(metrics.eligible_repeat_extras==1U);
    assert(metrics.compatible_repeated_draws==0U);
    assert(metrics.state_split_extras==1U);
    rows={first,first};
    metrics=audit_gpu_3do_instancing(rows);
    assert(metrics.source_draws==2U);
    assert(metrics.geometry_repeated_draws==0U);
    assert(metrics.repeated_member_ranges==0U);

    // Real scheduler batches nonadjacent exact opaque matches but leaves a
    // mutated CloneModel snapshot and incompatible ambient unbatched.
    rows={record(a,shared->groups[0],0x1000U,0x11U),
          record(changed,clone->groups[0],0x1000U,0x44U),
          record(b,shared->groups[0],0x1000U,0x22U)};
    auto schedule=schedule_gpu_3do_instancing(rows);
    assert(schedule.size()==3U);
    assert(schedule[0].source_index==0U && schedule[0].batch_id!=0U);
    assert(schedule[1].source_index==2U &&
           schedule[1].batch_id==schedule[0].batch_id);
    assert(schedule[2].source_index==1U && schedule[2].batch_id==0U);
    rows[2]=record(differing,shared->groups[0],0x1000U,0x22U);
    schedule=schedule_gpu_3do_instancing(rows);
    for(size_t i=0;i<schedule.size();++i) {
        assert(schedule[i].source_index==i);
        assert(schedule[i].batch_id==0U);
    }
    rows[2]=record(b,shared->groups[0],0x1000U,0x22U,true);
    schedule=schedule_gpu_3do_instancing(rows);
    for(auto const &item:schedule)assert(item.batch_id==0U);
    rows={first,first};
    schedule=schedule_gpu_3do_instancing(rows);
    for(auto const &item:schedule)assert(item.batch_id==0U);

    // Contiguous records for one owner must still split on exact state,
    // material and alpha policy; the mesh classification is not a
    // substitute for complete range compatibility.
    rows={first,record(differing,shared->groups[0],0x1000U,0x11U),
          record(b,shared->groups[0],0x2000U,0x22U),
          record(b,shared->groups[0],0x1000U,0x22U,false,
                 gpu_3do_alpha_mode::masked),
          record(b,shared->groups[0],0x1000U,0x22U)};
    schedule=schedule_gpu_3do_instancing(rows);
    assert(schedule.size()==5U);
    assert(schedule[0].source_index==0U && schedule[0].batch_id!=0U);
    assert(schedule[1].source_index==4U &&
           schedule[1].batch_id==schedule[0].batch_id);
    for(size_t i=2;i<schedule.size();++i) {
        assert(schedule[i].source_index==i-1U);
        assert(schedule[i].batch_id==0U);
    }

    // Direct collector: exact mesh/state classification, indexed group
    // slots, material/policy variants, duplicate guard and frame reset.
    {
        struct member { size_t instance_index; };
        gpu_3do_indexed_instancing_collector<member> collector;
        collector.reset();
        size_t const mesh=collector.classify(
            first.mesh_id,first.geometry,first.state,2U);
        assert(collector.classify(first.mesh_id,first.geometry,first.state,2U)==mesh);
        auto &layout=collector.collection_layout(mesh);
        assert(layout.size()==2U && layout[0].material==0U && layout[0].slot==0U);
        layout[0].material=0x1000U;
        layout[0].policy=gpu_3do_alpha_mode::masked;
        layout[0].slot=17U;
        assert(collector.collection_layout(mesh)[0].slot==17U);
        assert(collector.collection_layout(mesh)[1].material==0U);
        auto alternate_state=first.state;
        ++alternate_state[0];
        size_t const other=collector.classify(
            first.mesh_id,first.geometry,alternate_state,2U);
        assert(other!=mesh);
        assert(collector.classify(first.mesh_id+1U,first.geometry,first.state,2U)!=mesh);
        assert(collector.classify(first.mesh_id,first.geometry+1U,first.state,2U)!=mesh);
        size_t evaluations=0U;
        auto yes=[&] { ++evaluations; return true; };
        auto no=[&] { ++evaluations; return false; };
        auto const solid=gpu_3do_alpha_mode::solid;
        auto const masked=gpu_3do_alpha_mode::masked;
        size_t const slot=collector.find_range(mesh,0U,0x1000U,solid,yes);
        assert(collector.find_range(mesh,0U,0x1000U,solid,no)==slot);
        assert(evaluations==1U);
        assert(collector.append(slot,{1U}));
        assert(!collector.append(slot,{1U}));
        assert(collector.append(slot,{3U}));
        assert(!collector.append(slot,{2U}));
        assert(collector.range(slot).members.size()==2U);
        gpu_3do_geometry partition_geo;
        partition_geo.groups.resize(2U);
        partition_geo.face_planes.resize(2U);
        partition_geo.groups[0].count=3U;
        partition_geo.groups[0].plane_count=1U;
        partition_geo.groups[1].count=3U;
        std::vector<size_t> const partition_order{1U,0U};
        collector.mark_collection_slot(mesh,0U,slot);
        auto const &partition=collector.fast_layout(mesh,partition_geo,partition_order);
        assert(partition.anchored.size()==1U && partition.remaining.size()==1U);
        assert(partition.anchored[0].group_index==0U &&
               partition.anchored[0].slot==slot &&
               partition.anchored[0].plane_count==1U);
        assert(partition.remaining[0]==1U);
        auto const second_slot=collector.find_range(mesh,1U,0x1000U,solid,yes);
        assert(collector.append(second_slot,{3U}));
        collector.collection_layout(mesh)[1].material=0x1000U;
        collector.mark_collection_slot(mesh,1U,second_slot);
        auto const &updated=collector.fast_layout(mesh,partition_geo,partition_order);
        assert(updated.anchored.size()==2U && updated.remaining.empty());
        assert(updated.anchored[0].group_index==1U && updated.anchored[1].group_index==0U);
        assert(collector.find_range(mesh,1U,0x1000U,solid,yes)!=slot);
        assert(collector.find_range(mesh,0U,0x2000U,solid,yes)!=slot);
        assert(collector.find_range(mesh,0U,0x1000U,masked,yes)!=slot);
        assert(collector.find_range(other,0U,0x1000U,solid,yes)!=slot);
        size_t const rejected=collector.find_range(mesh,0U,0x3000U,solid,no);
        assert(!collector.append(rejected,{4U}));
        assert(collector.find_range(mesh,0U,0x3000U,solid,yes)==rejected);
        assert(!collector.append(rejected,{5U}));
        assert(collector.find_range(mesh,0U,0x1000U,solid,yes)==slot);
        collector.reset();
        auto const fresh=collector.classify(first.mesh_id,first.geometry,first.state,2U);
        assert(collector.collection_layout(fresh)[0].material==0U);
        assert(collector.collection_layout(fresh)[0].slot==0U);
        auto const fresh_slot=collector.find_range(fresh,0U,0x1000U,solid,yes);
        assert(collector.range(fresh_slot).members.empty());
        assert(collector.append(fresh_slot,{0U}));
    }

    // SIMD lanes classify different groups. Exact edge-on and near-edge
    // lanes must not be classified without the original face test.
    {
        struct simd_test_member { size_t instance_index; };
        using collector_type=gpu_3do_indexed_instancing_collector<simd_test_member>;
        collector_type::fast_plane_block block{};
        block.nx={{1,1,1,1}};
        block.d={{-2,2,0,0.00001f}};
        auto const result=block.classify({{0,0,0}},0.0001f);
        assert(result.facing==1U && result.uncertain==12U);
        collector_type collector;
        gpu_3do_geometry geo;
        geo.groups.resize(7U);
        geo.face_planes.resize(8U);
        std::vector<size_t> order{0,1,2,3,4,5,6};
        auto const mesh=collector.classify(1,1,{},7U);
        auto &layout=collector.collection_layout(mesh);
        for(size_t i=0;i<7U;++i) {
            geo.groups[i].count=3U;
            geo.groups[i].plane_start=uint32_t(i);
            geo.groups[i].plane_count=1U;
            geo.face_planes[i].normal={{1,0,0}};
            geo.face_planes[i].anchor={{float(i)-3,0,0}};
            layout[i].material=1U;
            collector.mark_collection_slot(mesh,i,i+1U);
        }
        geo.groups[1].face_type=1U; // double-sided
        geo.groups[6].plane_count=2U; // multi-plane
        auto const &fast=collector.fast_layout(mesh,geo,order,true);
        if(collector_type::simd_backfaces_supported) {
            assert(fast.plane_blocks.size()==1U);
            assert(fast.scalar_indices.size()==3U);
            auto const &b=fast.plane_blocks[0];
            assert((b.indices==std::array<size_t,4>{{0,2,3,4}}));
            auto const eye=std::array<float,3>{{0,0,0}};
            auto const test=prepare_gpu_3do_face_plane_test(geo,eye,true);
            auto const masks=b.classify(eye,test.margin);
            assert(masks.facing==3U && masks.uncertain==4U);
            collector.reset();
            auto const fresh=collector.classify(1,1,{},7U);
            collector.collection_layout(fresh);
            auto const &cleared=collector.fast_layout(fresh,geo,order,true);
            assert(cleared.plane_blocks.empty() && cleared.remaining.size()==7U);
        }
    }
    // Duplicate owner records stay separate, while distinct owners batch
    // in first-appearance order even when their records are interleaved.
    rows={record(a,shared->groups[0],0x1000U,0x11U),
          record(a,shared->groups[0],0x1000U,0x11U),
          record(b,shared->groups[0],0x1000U,0x22U),
          record(a,shared->groups[0],0x1000U,0x11U),
          record(b,shared->groups[0],0x1000U,0x33U),
          record(b,shared->groups[0],0x1000U,0x22U)};
    schedule=schedule_gpu_3do_instancing(rows);
    assert(schedule.size()==rows.size());
    assert(schedule[0].source_index==0U && schedule[0].batch_id!=0U);
    assert(schedule[1].source_index==2U &&
           schedule[1].batch_id==schedule[0].batch_id);
    assert(schedule[2].source_index==4U &&
           schedule[2].batch_id==schedule[0].batch_id);
    assert(schedule[3].source_index==1U && schedule[3].batch_id==0U);
    assert(schedule[4].source_index==3U && schedule[4].batch_id==0U);
    assert(schedule[5].source_index==5U && schedule[5].batch_id==0U);

    // Large repeated-model workload: every exact material/policy group
    // batches 512 distinct Things, while duplicate owner records do not.
    // Re-run to verify generation-stamped scratch never leaks prior owners.
    std::vector<gpu_3do_instancing_audit_record> stress;
    stress.reserve(512U*8U);
    // Static storage avoids MSVC treating the lambda's array bound as
    // a captured automatic variable rather than a constant expression.
    static constexpr size_t owner_count=512U,material_count=7U;
    for(size_t owner=0;owner<owner_count;++owner) {
        for(size_t material=0;material<material_count;++material) {
            auto item=record(a,shared->groups[0],
                uintptr_t(0x1000U+material*64U),
                uintptr_t(0x10000U+owner),false,
                material%2U?gpu_3do_alpha_mode::masked:
                            gpu_3do_alpha_mode::solid);
            stress.push_back(item);
            if(owner%31U==0U)stress.push_back(item);
        }
    }
    auto check_stress=[&]() {
        auto const scheduled=schedule_gpu_3do_instancing(stress);
        assert(scheduled.size()==stress.size());
        std::vector<bool> seen(stress.size(),false);
        std::array<size_t,material_count> batch_ids{};
        std::array<size_t,material_count> batched{};
        for(auto const &entry:scheduled) {
            assert(entry.source_index<stress.size());
            assert(!seen[entry.source_index]);
            seen[entry.source_index]=true;
            auto const &item=stress[entry.source_index];
            auto const material_index=
                (item.material-uintptr_t(0x1000U))/uintptr_t(64U);
            assert(material_index<material_count);
            if(entry.batch_id!=0U) {
                ++batched[material_index];
                if(batch_ids[material_index]==0U)
                    batch_ids[material_index]=entry.batch_id;
                else assert(batch_ids[material_index]==entry.batch_id);
            }
        }
        for(bool v:seen)assert(v);
        for(auto count:batched)assert(count==owner_count);
        for(size_t i=0;i<material_count;++i)
            for(size_t j=i+1;j<material_count;++j)
                assert(batch_ids[i]!=batch_ids[j]);
    };
    check_stress();
    check_stress();
    stress.clear();
    stress.push_back(record(a,shared->groups[0],0x1000U,0x11U));
    assert(schedule_gpu_3do_instancing(stress)[0].batch_id==0U);

    // Window aggregation must add histograms slot-by-slot, including
    // both "all ranges" and the more relevant "repeat members" set.
    profile_sample frame{},window{};
    frame.gpu_instancing_rejected_ranges=1U;
    frame.gpu_instancing_prepare_us=11U;
    frame.gpu_instancing_audit_us=23U;
    frame.gpu_instancing_schedule_us=37U;
    frame.gpu_instancing_reorder_us=7U;
    frame.gpu_instancing_repeated_member_ranges=2U;
    frame.gpu_instancing_alpha_policies[
        static_cast<size_t>(gpu_3do_alpha_mode::masked)]=3U;
    frame.gpu_instancing_rejects_all[static_cast<size_t>(Reason::normal_map)]=1U;
    frame.gpu_instancing_rejects_repeated[static_cast<size_t>(Reason::normal_map)]=1U;
    window.add(frame);
    window.add(frame);
    assert(window.gpu_instancing_rejected_ranges==2U);
    assert(window.gpu_instancing_prepare_us==22U);
    assert(window.gpu_instancing_audit_us==46U);
    assert(window.gpu_instancing_schedule_us==74U);
    assert(window.gpu_instancing_reorder_us==14U);
    assert(window.gpu_instancing_repeated_member_ranges==4U);
    assert(window.gpu_instancing_alpha_policies[
        static_cast<size_t>(gpu_3do_alpha_mode::masked)]==6U);
    assert(window.gpu_instancing_rejects_all[static_cast<size_t>(Reason::normal_map)]==2U);
    assert(window.gpu_instancing_rejects_repeated[static_cast<size_t>(Reason::normal_map)]==2U);
    return 0;
}
