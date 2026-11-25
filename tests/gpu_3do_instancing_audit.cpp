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

    // Window aggregation must add histograms slot-by-slot, including
    // both "all ranges" and the more relevant "repeat members" set.
    profile_sample frame{},window{};
    frame.gpu_instancing_rejected_ranges=1U;
    frame.gpu_instancing_repeated_member_ranges=2U;
    frame.gpu_instancing_rejects_all[static_cast<size_t>(Reason::normal_map)]=1U;
    frame.gpu_instancing_rejects_repeated[static_cast<size_t>(Reason::normal_map)]=1U;
    window.add(frame);
    window.add(frame);
    assert(window.gpu_instancing_rejected_ranges==2U);
    assert(window.gpu_instancing_repeated_member_ranges==4U);
    assert(window.gpu_instancing_rejects_all[static_cast<size_t>(Reason::normal_map)]==2U);
    assert(window.gpu_instancing_rejects_repeated[static_cast<size_t>(Reason::normal_map)]==2U);
    return 0;
}
