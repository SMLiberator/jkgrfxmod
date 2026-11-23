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
#include <vector>

namespace jkgm {
struct gpu_3do_instancing_audit_record {
    uint64_t mesh_id=0;
    uintptr_t geometry=0,group=0,material=0,instance=0;
    std::array<uint32_t,11> state{};
    bool state_eligible=false;
};
struct gpu_3do_instancing_audit_stats {
    uint64_t source_draws=0,geometry_repeated_draws=0;
    uint64_t geometry_repeated_groups=0,conservative_draws=0;
    uint64_t compatible_repeated_draws=0,compatible_groups=0;
};
inline uint32_t gpu_3do_instancing_float_bits(float x) {
    uint32_t bits=0;
    std::memcpy(&bits,&x,sizeof(bits));
    return bits;
}
// Only full, opaque, unmodified model draws without per-Thing lights or
// view-dependent fragment state. This predicate is intentionally stricter
// than final instancing will need to be; it is not used for actual draws.
inline bool gpu_3do_instancing_conservative_candidate(
    gpu_3do_instance const &inst,gpu_3do_group const &group,
    gpu_3do_alpha_mode policy,bool material_view_dependent)
{
    return inst.id!=0 && !!inst.geometry &&
        policy==gpu_3do_alpha_mode::solid &&
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
    using compatible_key=std::tuple<geometry_key,std::array<uint32_t,11>>;
    std::map<geometry_key,std::set<uintptr_t>> geometric;
    std::map<compatible_key,std::set<uintptr_t>> compatible;
    gpu_3do_instancing_audit_stats result{};
    for(auto const &r:records) {
        if(!r.mesh_id || !r.geometry || !r.group || !r.material ||
           !r.instance)continue;
        ++result.source_draws;
        geometry_key const identity{
            r.mesh_id,r.geometry,r.group,r.material};
        geometric[identity].insert(r.instance);
        if(r.state_eligible) {
            ++result.conservative_draws;
            compatible[{identity,r.state}].insert(r.instance);
        }
    }
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
    return result;
}
} // namespace jkgm
