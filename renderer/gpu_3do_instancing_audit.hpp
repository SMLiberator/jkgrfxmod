#pragma once
// Profile-only feasibility audit for ordinary GPU 3DO instancing.
// A CloneModel retains a filename, NOT an immutable mesh identity: only
// the current cache snapshot ID AND exact geometry/group address match.
#include "gpu_3do_preview.hpp"
#include "gpu_3do_alpha.hpp"
#include <array>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <map>
#include <set>
#include <tuple>
#include <unordered_set>
#include <vector>

namespace jkgm {
// Profile-only multi-reason bitmask. Reasons may overlap for one pending
// opaque range, and CloneModel filenames do not establish mesh identity.
enum class gpu_3do_instancing_rejection : uint32_t {
    missing_geometry, alpha_policy, cutout_face, specular, normal_map,
    surface_map, environment_map, matcap, clearcoat, displacement,
    shadow_only, projected_decal, sprite, transient, saber_trail,
    camera_hud, skybox_lighting, dither, opacity, per_thing_lights,
    crossing_lighting, nonrigid_lighting, invalid_state, count
};
// Masked fragments discard by cutoff and write depth in the opaque pass.
// Mixed-alpha, blended and dithered materials retain conservative handling.
inline constexpr bool gpu_3do_instancing_opaque_policy(
    gpu_3do_alpha_mode policy) {
    return policy==gpu_3do_alpha_mode::solid ||
           policy==gpu_3do_alpha_mode::masked;
}
inline constexpr size_t gpu_3do_instancing_alpha_policy_count=5U;
static_assert(static_cast<size_t>(gpu_3do_alpha_mode::dithered)+1U==
                  gpu_3do_instancing_alpha_policy_count,
              "Update 3DO instancing alpha histogram when modes change");
inline constexpr size_t gpu_3do_instancing_rejection_count=
    static_cast<size_t>(gpu_3do_instancing_rejection::count);
static_assert(gpu_3do_instancing_rejection_count<=32U,
    "Rejection audit uses a 32-bit bitmask");
inline constexpr uint32_t gpu_3do_instancing_rejection_bit(
    gpu_3do_instancing_rejection r) {
    return 1U<<static_cast<uint32_t>(r);
}
inline constexpr std::array<char const *,
    gpu_3do_instancing_rejection_count> gpu_3do_instancing_rejection_names{{
    "missing_geometry","alpha_policy","cutout_face","specular",
    "normal_map","surface_map","environment_map","matcap","clearcoat",
    "displacement","shadow_only","projected_decal","sprite","transient",
    "saber_trail","camera_hud","skybox_lighting","dither","opacity",
    "per_thing_lights","crossing_lighting","nonrigid_lighting","invalid_state"
}};
struct gpu_3do_instancing_audit_record {
    uint64_t mesh_id=0;
    uintptr_t geometry=0,group=0,material=0,instance=0;
    gpu_3do_alpha_mode policy=gpu_3do_alpha_mode::solid;
    std::array<uint32_t,11> state{};
    bool state_eligible=false;
    uint32_t rejection_mask=0U;
};
struct gpu_3do_instancing_audit_stats {
    uint64_t source_draws=0,geometry_repeated_draws=0;
    uint64_t geometry_repeated_groups=0,conservative_draws=0;
    uint64_t compatible_repeated_draws=0,compatible_groups=0;
    // Member ranges include the first distinct Thing; extras exclude it.
    uint64_t repeated_member_ranges=0,repeated_eligible_member_ranges=0;
    uint64_t rejected_ranges=0,repeated_rejected_ranges=0;
    uint64_t eligible_repeat_extras=0,state_split_extras=0;
    std::array<uint64_t,gpu_3do_instancing_rejection_count> rejects_all{};
    std::array<uint64_t,gpu_3do_instancing_rejection_count> rejects_repeated{};
    std::array<uint64_t,gpu_3do_instancing_alpha_policy_count> alpha_policy_ranges{};
};
inline uint32_t gpu_3do_instancing_float_bits(float x) {
    uint32_t bits=0;
    std::memcpy(&bits,&x,sizeof(bits));
    return bits;
}
// Solid or masked opaque model draws without per-Thing lights or
// view-dependent state. The live scheduler also uses this strict predicate.
inline bool gpu_3do_instancing_conservative_candidate(
    gpu_3do_instance const &inst,gpu_3do_group const &group,
    gpu_3do_alpha_mode policy,bool material_view_dependent)
{
    return inst.id!=0 && !!inst.geometry &&
        gpu_3do_instancing_opaque_policy(policy) &&
        (group.face_type&0x2U)==0U &&
        !material_view_dependent &&
        !inst.shadow_only && !inst.is_projected_decal &&
        !inst.is_sprite_quad && !inst.transient_procedural &&
        !inst.saber_trail_active && !inst.camera_hud &&
        !inst.skybox_vertex_lighting &&
        !inst.thing_dither && inst.thing_opacity==1.0f &&
        inst.num_lights==0U && !inst.crossing_lighting &&
        !inst.nonrigid_light_transform;
}
// The rejection mask mirrors eligibility and distinguishes material features.
// material_rejections must be computed from the resolved material.
inline uint32_t gpu_3do_instancing_rejection_mask(
    gpu_3do_instance const &inst,gpu_3do_group const &group,
    gpu_3do_alpha_mode policy,uint32_t material_rejections,bool state_valid)
{
    using R=gpu_3do_instancing_rejection;
    uint32_t result=material_rejections;
    auto reject=[&](bool test,R reason) {
        if(test)result|=gpu_3do_instancing_rejection_bit(reason);
    };
    reject(inst.id==0U || !inst.geometry,R::missing_geometry);
    reject(!gpu_3do_instancing_opaque_policy(policy),R::alpha_policy);
    reject((group.face_type&0x2U)!=0U,R::cutout_face);
    reject(inst.shadow_only,R::shadow_only);
    reject(inst.is_projected_decal,R::projected_decal);
    reject(inst.is_sprite_quad,R::sprite);
    reject(inst.transient_procedural,R::transient);
    reject(inst.saber_trail_active,R::saber_trail);
    reject(inst.camera_hud,R::camera_hud);
    reject(inst.skybox_vertex_lighting,R::skybox_lighting);
    reject(inst.thing_dither,R::dither);
    reject(inst.thing_opacity!=1.0f,R::opacity);
    reject(inst.num_lights!=0U,R::per_thing_lights);
    reject(static_cast<bool>(inst.crossing_lighting),R::crossing_lighting);
    reject(inst.nonrigid_light_transform,R::nonrigid_lighting);
    reject(!state_valid,R::invalid_state);
    return result;
}
inline bool gpu_3do_instancing_capture_state(
    gpu_3do_instance const &inst,bool reversed_winding,
    std::array<uint32_t,11> &state)
{
    if(!std::isfinite(inst.ambient) || !std::isfinite(inst.attenuation) ||
       !std::isfinite(inst.effective_light_mode))return false;
    for(float x:inst.colormap_tint)if(!std::isfinite(x))return false;
    for(float x:inst.sector_tint)if(!std::isfinite(x))return false;
    state[0]=gpu_3do_instancing_float_bits(inst.ambient);
    state[1]=gpu_3do_instancing_float_bits(inst.attenuation);
    state[2]=gpu_3do_instancing_float_bits(inst.effective_light_mode);
    for(size_t i=0;i<3;++i) {
        state[3+i]=gpu_3do_instancing_float_bits(inst.colormap_tint[i]);
        state[6+i]=gpu_3do_instancing_float_bits(inst.sector_tint[i]);
    }
    state[9]=inst.effective_geometry_mode;
    state[10]=uint32_t(reversed_winding) |
        (uint32_t(inst.world_lighting_exempt)<<1U) |
        (uint32_t(inst.sector_tint_valid)<<2U);
    return true;
}
inline gpu_3do_instancing_audit_stats audit_gpu_3do_instancing(
    std::vector<gpu_3do_instancing_audit_record> const &records)
{
    using geometry_key=std::tuple<uint64_t,uintptr_t,uintptr_t,uintptr_t>;
    using compatible_key=std::tuple<geometry_key,gpu_3do_alpha_mode,
                                    std::array<uint32_t,11>>;
    std::map<geometry_key,std::set<uintptr_t>> geometric;
    std::map<geometry_key,std::set<uintptr_t>> eligible;
    std::map<compatible_key,std::set<uintptr_t>> compatible;
    gpu_3do_instancing_audit_stats result{};
    for(auto const &r:records) {
        if(!r.mesh_id || !r.geometry || !r.group || !r.material ||
           !r.instance)continue;
        ++result.source_draws;
        size_t const alpha_index=static_cast<size_t>(r.policy);
        if(alpha_index<result.alpha_policy_ranges.size())
            ++result.alpha_policy_ranges[alpha_index];
        geometry_key const identity{
            r.mesh_id,r.geometry,r.group,r.material};
        geometric[identity].insert(r.instance);
        if(r.rejection_mask!=0U)++result.rejected_ranges;
        for(size_t i=0;i<gpu_3do_instancing_rejection_count;++i)
            if((r.rejection_mask&(1U<<i))!=0U)
                ++result.rejects_all[i];
        if(r.state_eligible) {
            ++result.conservative_draws;
            eligible[identity].insert(r.instance);
            compatible[{identity,r.policy,r.state}].insert(r.instance);
        }
    }
    // Only geometry shared by distinct Things can benefit from cross-Thing
    // instancing. Repeated ranges from one Thing do not create candidates.
    for(auto const &r:records) {
        if(!r.mesh_id || !r.geometry || !r.group || !r.material ||
           !r.instance)continue;
        geometry_key const identity{r.mesh_id,r.geometry,r.group,r.material};
        auto const found=geometric.find(identity);
        if(found==geometric.end() || found->second.size()<2U)continue;
        ++result.repeated_member_ranges;
        if(r.state_eligible)++result.repeated_eligible_member_ranges;
        if(r.rejection_mask!=0U)++result.repeated_rejected_ranges;
        for(size_t i=0;i<gpu_3do_instancing_rejection_count;++i)
            if((r.rejection_mask&(1U<<i))!=0U)
                ++result.rejects_repeated[i];
    }
    for(auto const &entry:eligible)
        if(entry.second.size()>1U)
            result.eligible_repeat_extras+=entry.second.size()-1U;
    for(auto const &entry:geometric)
        if(entry.second.size()>1U) {
            ++result.geometry_repeated_groups;
            result.geometry_repeated_draws+=entry.second.size()-1U;
        }
    for(auto const &entry:compatible)
        if(entry.second.size()>1U) {
            ++result.compatible_groups;
            result.compatible_repeated_draws+=entry.second.size()-1U;
        }
    // Identical eligible geometry with different policy, ambient, tint or
    // winding cannot share the current strict per-draw state.
    result.state_split_extras=
        result.eligible_repeat_extras-result.compatible_repeated_draws;
    return result;
}
// Opaque-only, exact-identity schedule for real main-camera instancing.
// CloneModel filenames never establish identical GPU geometry/materials.
struct gpu_3do_instancing_schedule_entry {
    size_t source_index=0;
    size_t batch_id=0; // zero => unchanged individual draw
};
inline std::vector<gpu_3do_instancing_schedule_entry>
schedule_gpu_3do_instancing(
    std::vector<gpu_3do_instancing_audit_record> const &records)
{
    using geometry_key=std::tuple<uint64_t,uintptr_t,uintptr_t,uintptr_t>;
    using batch_key=std::tuple<geometry_key,gpu_3do_alpha_mode,
                               std::array<uint32_t,11>>;
    struct batch_members {
        std::vector<size_t> indices;
        std::unordered_set<uintptr_t> owners;
    };
    std::map<batch_key,batch_members> groups;
    for(size_t i=0;i<records.size();++i) {
        auto const &r=records[i];
        if(!r.state_eligible || r.rejection_mask!=0U ||
           !r.mesh_id || !r.geometry || !r.group || !r.material ||
           !r.instance)continue;
        geometry_key const geometry{r.mesh_id,r.geometry,r.group,r.material};
        batch_key const key{geometry,r.policy,r.state};
        auto &members=groups[key];
        // In repeated-model stress scenes, each material group can appear
        // hundreds of times. Avoid a quadratic scan for duplicate Things.
        if(members.owners.insert(r.instance).second)
            members.indices.push_back(i);
    }
    std::vector<size_t> batch_of(records.size(),0U);
    std::vector<std::vector<size_t>> batches(1);
    for(auto const &entry:groups) {
        if(entry.second.indices.size()<2U)continue;
        size_t const batch_id=batches.size();
        batches.push_back(entry.second.indices);
        for(size_t index:entry.second.indices)batch_of[index]=batch_id;
    }
    std::vector<bool> emitted(batches.size(),false);
    std::vector<gpu_3do_instancing_schedule_entry> ordered;
    ordered.reserve(records.size());
    for(size_t i=0;i<records.size();++i) {
        size_t const id=batch_of[i];
        if(id==0U)ordered.push_back({i,0U});
        else if(!emitted[id]) {
            for(size_t index:batches[id])
                ordered.push_back({index,id});
            emitted[id]=true;
        }
    }
    return ordered;
}
} // namespace jkgm
