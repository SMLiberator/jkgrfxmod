#pragma once
// CPU-only main-camera 3DO visibility prepass. Inputs are immutable cached
// mesh snapshots plus a CAPTURED camera; never access live JK/COG/GL state.
// A result is indexed by the original instance and group indices so the
// serial material/alpha/HUD draw order remains identical.
#include "gpu_3do_preview.hpp"
#include "gpu_3do_compat.hpp"
#include "gpu_3do_frustum.hpp"
#include <array>
#include <cstdint>
#include <vector>

namespace jkgm {
enum class gpu_3do_cull_reason : uint8_t {
    visible=0U,frustum=1U,backface=2U
};
struct gpu_3do_parallel_visibility {
    gpu_3do_matrix34 modelview{};
    std::array<float,3> local_eye{};
    std::vector<gpu_3do_cull_reason> groups;
    // Preserve the original indexed flags for specialized collection and
    // fallback. The optional compact list follows cached opaque order.
    std::vector<size_t> visible_opaque_groups;
    size_t compact_frustum_rejected=0U,compact_backface_rejected=0U;
    bool compact_ready=false;
    bool ready=false,mesh_outside=false,mesh_inside=false;
    size_t group_frustum_tests_avoided=0U;
    bool has_local_eye=false,reversed_winding=false;
};

// Reused per-Thing output owns cull flags and optional ordered indices; it never modifies
// shared geometry. cached_group_bounds() uses std::call_once internally.
inline void prepare_gpu_3do_parallel_visibility(
    gpu_3do_instance const &inst,
    gpu_3do_matrix34 const &native_view,
    gpu_3do_native_frustum const &frustum,
    bool have_perspective_frustum,
    gpu_3do_parallel_visibility &out,
    bool accept_whole_mesh=false,bool compact_backfaces=false,
    bool compact_visibility=false)
{
    out.ready=false;
    out.compact_ready=false;
    out.visible_opaque_groups.clear();
    out.compact_frustum_rejected=out.compact_backface_rejected=0U;
    out.mesh_outside=false;
    out.mesh_inside=false;
    out.group_frustum_tests_avoided=0U;
    if(!inst.geometry)return;
    auto const &geo=*inst.geometry;
    out.modelview=gpu_3do_native_modelview(native_view,inst.transform);
    std::array<float,3> mesh_min{},mesh_max{};
    if(have_perspective_frustum &&
       geo.cached_group_bounds(mesh_min,mesh_max) &&
       gpu_3do_batch_outside_frustum(
           frustum,out.modelview,mesh_min,mesh_max,
           accept_whole_mesh?&out.mesh_inside:nullptr)) {
        out.mesh_outside=true;
        out.ready=true;
        return;
    }
    out.has_local_eye=gpu_3do_local_eye(out.modelview,out.local_eye);
    out.reversed_winding=gpu_3do_reversed_winding(inst.transform);
    auto const plane_test=prepare_gpu_3do_face_plane_test(
        geo,out.local_eye,compact_backfaces && out.has_local_eye);
    out.groups.resize(geo.groups.size());
    for(size_t i=0;i<geo.groups.size();++i) {
        auto const &group=geo.groups[i];
        auto reason=gpu_3do_cull_reason::visible;
        bool const accepted=out.mesh_inside && group.count!=0U;
        if(accepted)++out.group_frustum_tests_avoided;
        if(have_perspective_frustum && !accepted &&
           gpu_3do_batch_outside_frustum(
               frustum,out.modelview,
               group.bounds_min,group.bounds_max)) {
            reason=gpu_3do_cull_reason::frustum;
        } else if(out.has_local_eye && !(group.face_type&0x1U) &&
                  group.plane_count>0 &&
                  group.plane_start+group.plane_count<=geo.face_planes.size()) {
            if(!gpu_3do_group_has_facing_plane(
                geo,group,out.local_eye,plane_test))
                reason=gpu_3do_cull_reason::backface;
        }
        out.groups[i]=reason;
    }
    // Leave fully contained meshes on the existing specialized path.
    // For sparse intersecting meshes, move the second visibility walk
    // onto the workers and avoid expensive serial metadata visits.
    if(compact_visibility && !out.mesh_inside && geo.groups.size()>=32U) {
        for(auto index:geo.cached_opaque_group_order()) {
            auto const reason=out.groups[index];
            if(reason==gpu_3do_cull_reason::frustum)
                ++out.compact_frustum_rejected;
            else if(reason==gpu_3do_cull_reason::backface)
                ++out.compact_backface_rejected;
            else out.visible_opaque_groups.push_back(index);
        }
        out.compact_ready=out.visible_opaque_groups.size()<=geo.groups.size()/2U;
    }
    out.ready=true;
}
} // namespace jkgm
