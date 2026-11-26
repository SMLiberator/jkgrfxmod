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
#include <unordered_map>
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
    // Classify the mesh and shared lighting state once per contiguous
    // instance run. Only the smaller range key is hashed for each face
    // group; full equality remains mandatory at both levels.
    using mesh_key=std::tuple<uint64_t,uintptr_t,std::array<uint32_t,11>>;
    using batch_key=std::tuple<size_t,uintptr_t,uintptr_t,gpu_3do_alpha_mode>;
    struct mesh_key_hash {
        size_t operator()(mesh_key const &key) const noexcept {
            size_t hash=0x9e3779b9U;
            auto mix=[&](uint64_t value) {
                size_t const x=static_cast<size_t>(value) ^
                    static_cast<size_t>(value>>32U);
                hash^=x+0x9e3779b9U+(hash<<6U)+(hash>>2U);
            };
            mix(std::get<0>(key));
            mix(std::get<1>(key));
            for(uint32_t value:std::get<2>(key))mix(value);
            return hash;
        }
    };
    struct batch_key_hash {
        size_t operator()(batch_key const &key) const noexcept {
            size_t hash=0x9e3779b9U;
            auto mix=[&](size_t value) {
                hash^=value+0x9e3779b9U+(hash<<6U)+(hash>>2U);
            };
            mix(std::get<0>(key));
            mix(std::get<1>(key));
            mix(std::get<2>(key));
            mix(static_cast<size_t>(std::get<3>(key)));
            return hash;
        }
    };
    struct owner_slot {
        size_t group=0U;
        uintptr_t instance=0U;
        uint32_t stamp=0U;
    };
    struct workspace {
        std::unordered_map<mesh_key,size_t,mesh_key_hash> mesh_lookup;
        std::unordered_map<batch_key,size_t,batch_key_hash> group_lookup;
        std::vector<std::vector<size_t>> group_indices;
        // Flat, generation-stamped open-addressed set for (group,Thing).
        // Avoid a separate heap allocation for every repeated model owner.
        std::vector<owner_slot> seen_owners;
        uint32_t stamp=0U;
    };
    static thread_local workspace scratch;
    scratch.mesh_lookup.clear();
    scratch.group_lookup.clear();
    size_t group_count=0U;
    size_t mesh_count=0U;
    mesh_key previous_mesh{};
    uintptr_t previous_instance=0U;
    size_t mesh_class=0U;
    if(++scratch.stamp==0U) {
        for(auto &slot:scratch.seen_owners)slot.stamp=0U;
        scratch.stamp=1U;
    }
    // <=50% occupancy for short probe chains, power-of-two capacity.
    // Slots persist across frames; their generation stamps prevent stale
    // owners from contaminating the next frame or a different world.
    size_t table_size=16U;
    while(table_size<=records.size()*2U)table_size*=2U;
    if(scratch.seen_owners.size()<table_size)
        scratch.seen_owners.resize(table_size);
    size_t const owner_mask=scratch.seen_owners.size()-1U;
    auto first_owner=[&](size_t group,uintptr_t instance) {
        size_t hash=static_cast<size_t>(instance);
        hash^=group+0x9e3779b9U+(hash<<6U)+(hash>>2U);
        hash^=hash>>16U;
        hash*=0x7feb352dU;
        hash^=hash>>15U;
        hash*=0x846ca68bU;
        hash^=hash>>16U;
        for(size_t offset=hash&owner_mask;;offset=(offset+1U)&owner_mask) {
            auto &slot=scratch.seen_owners[offset];
            if(slot.stamp!=scratch.stamp) {
                slot={group,instance,scratch.stamp};
                return true;
            }
            if(slot.group==group && slot.instance==instance)
                return false;
        }
    };
    for(size_t i=0;i<records.size();++i) {
        auto const &r=records[i];
        if(!r.state_eligible || r.rejection_mask!=0U ||
           !r.mesh_id || !r.geometry || !r.group || !r.material ||
           !r.instance)continue;
        // The public scheduler also accepts arbitrary record order and
        // even inconsistent state for a repeated instance. Verify the
        // complete key before reusing the previous classification.
        if(previous_instance!=r.instance ||
           std::get<0>(previous_mesh)!=r.mesh_id ||
           std::get<1>(previous_mesh)!=r.geometry ||
           std::get<2>(previous_mesh)!=r.state) {
            previous_mesh=mesh_key{r.mesh_id,r.geometry,r.state};
            previous_instance=r.instance;
            auto const found=scratch.mesh_lookup.try_emplace(
                previous_mesh,mesh_count);
            mesh_class=found.first->second;
            if(found.second)++mesh_count;
        }
        batch_key const key{mesh_class,r.group,r.material,r.policy};
        auto const found=scratch.group_lookup.try_emplace(key,group_count);
        size_t const group=found.first->second;
        if(found.second) {
            ++group_count;
            if(scratch.group_indices.size()<group_count)
                scratch.group_indices.emplace_back();
            scratch.group_indices[group].clear();
        }
        if(first_owner(group,r.instance))
            scratch.group_indices[group].push_back(i);
    }
    // Batch IDs are temporary grouping tags; draw order is determined by
    // source order, never by the hash bucket layout.
    std::vector<size_t> batch_of(records.size(),0U);
    std::vector<std::vector<size_t> const *> batches(1U,nullptr);
    for(size_t group=0;group<group_count;++group) {
        auto const &indices=scratch.group_indices[group];
        if(indices.size()<2U)continue;
        size_t const batch_id=batches.size();
        batches.push_back(&indices);
        for(size_t index:indices)batch_of[index]=batch_id;
    }
    std::vector<bool> emitted(batches.size(),false);
    std::vector<gpu_3do_instancing_schedule_entry> ordered;
    ordered.reserve(records.size());
    for(size_t i=0;i<records.size();++i) {
        size_t const batch_id=batch_of[i];
        if(batch_id==0U)ordered.push_back({i,0U});
        else if(!emitted[batch_id]) {
            for(size_t index:*batches[batch_id])
                ordered.push_back({index,batch_id});
            emitted[batch_id]=true;
        }
    }
    return ordered;
}
// Three RGBA32F texels store affine rows; directions have w=0, origin w=1.
inline std::array<std::array<float,4>,3> gpu_3do_instance_transform_texels(
    std::array<float,12> const &m) {
    return {{{{m[0],m[3],m[6],m[9]}},
             {{m[1],m[4],m[7],m[10]}},
             {{m[2],m[5],m[8],m[11]}}}};
}
inline bool gpu_3do_instance_buffer_can_fit(
    size_t transforms,size_t membership_texels,size_t max_texels) {
    return transforms<=16777216U && transforms<=max_texels/3U &&
        membership_texels<=max_texels-transforms*3U;
}
// Main-camera collector: classify a mesh once per source instance, then
// address its immutable group slots directly. All slot IDs are frame-local;
// zero is the empty sentinel. Exact material/policy variants get distinct
// slots even when they share the same geometry and group index.
template<class Member>
class gpu_3do_indexed_instancing_collector {
    using mesh_key=std::tuple<uint64_t,uintptr_t,std::array<uint32_t,11>>;
    struct mesh_class {
        std::vector<size_t> heads;
    };
public:
    struct range_slot {
        uintptr_t material=0U;
        gpu_3do_alpha_mode policy=gpu_3do_alpha_mode::solid;
        size_t next=0U;
        size_t buffer_offset=0U;
        bool eligible=false;
        std::vector<Member> members;
    };
private:
    std::map<mesh_key,size_t> lookup;
    std::vector<mesh_class> classes;
    std::vector<range_slot> slots;
    size_t used_classes=0U,used_slots=0U;
public:
    void reset() {
        lookup.clear();
        used_classes=0U;
        used_slots=0U;
    }
    size_t classify(uint64_t mesh_id,uintptr_t geometry,
                    std::array<uint32_t,11> const &state,size_t group_count) {
        auto const found=lookup.try_emplace(
            mesh_key{mesh_id,geometry,state},used_classes);
        if(found.second) {
            if(classes.size()<=used_classes)classes.emplace_back();
            classes[used_classes++].heads.assign(group_count,0U);
        }
        return found.first->second;
    }
    template<class IsEligible>
    size_t find_range(size_t mesh,size_t group,uintptr_t material,
                      gpu_3do_alpha_mode policy,IsEligible &&is_eligible) {
        auto &head=classes[mesh].heads[group];
        for(size_t id=head;id!=0U;id=slots[id-1U].next) {
            auto const &slot=slots[id-1U];
            if(slot.material==material && slot.policy==policy)return id;
        }
        size_t const id=++used_slots;
        if(slots.size()<id)slots.emplace_back();
        auto &slot=slots[id-1U];
        slot.material=material;
        slot.policy=policy;
        slot.next=head;
        slot.eligible=is_eligible();
        slot.members.clear();
        head=id;
        return id;
    }
    range_slot &range(size_t id) { return slots[id-1U]; }
    range_slot const &range(size_t id) const { return slots[id-1U]; }
    // The renderer visits each source instance once, in ascending order.
    // Fail open for an accidental repeated or out-of-order member, leaving
    // that draw on the ordinary list instead of duplicating a batch owner.
    bool append(size_t id,Member const &member) {
        auto &slot=range(id);
        if(!slot.eligible || (!slot.members.empty() &&
           slot.members.back().instance_index>=member.instance_index))
            return false;
        slot.members.push_back(member);
        return true;
    }
};
} // namespace jkgm
