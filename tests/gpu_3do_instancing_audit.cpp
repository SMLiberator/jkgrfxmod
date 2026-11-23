#include "renderer/gpu_3do_instancing_audit.hpp"
#include "renderer/frame_profile.hpp"
#include <cassert>
#include <limits>
#include <memory>
#include <vector>

using namespace jkgm;

static gpu_3do_instancing_audit_record record(
    gpu_3do_instance const &inst,gpu_3do_group const &group,
    uintptr_t material,uintptr_t owner,bool view_dependent=false)
{
    gpu_3do_instancing_audit_record r{};
    r.mesh_id=inst.id;
    r.geometry=reinterpret_cast<uintptr_t>(inst.geometry.get());
    r.group=reinterpret_cast<uintptr_t>(&group);
    r.material=material;
    r.instance=owner;
    bool const valid_state=
        gpu_3do_instancing_capture_state(inst,false,r.state);
    r.state_eligible=gpu_3do_instancing_conservative_candidate(
        inst,group,gpu_3do_alpha_mode::solid,view_dependent) && valid_state;
    r.rejection_mask=gpu_3do_instancing_rejection_mask(
        inst,group,gpu_3do_alpha_mode::solid,
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
    excluded.crossing_lighting=true;
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
