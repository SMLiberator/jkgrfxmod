#include "renderer/gpu_3do_instancing_audit.hpp"
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
    r.state_eligible=gpu_3do_instancing_conservative_candidate(
        inst,group,gpu_3do_alpha_mode::solid,view_dependent) &&
        gpu_3do_instancing_capture_state(inst,false,r.state);
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
    return 0;
}
