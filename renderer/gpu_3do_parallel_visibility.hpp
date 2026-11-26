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
    bool ready=false,mesh_outside=false,mesh_inside=false;
    size_t group_frustum_tests_avoided=0U;
    bool has_local_eye=false,reversed_winding=false;
};

// Reused per-Thing output owns only group cull flags; it never modifies
// shared geometry. cached_group_bounds() uses std::call_once internally.
inline void prepare_gpu_3do_parallel_visibility(
    gpu_3do_instance const &inst,
    gpu_3do_matrix34 const &native_view,
    gpu_3do_native_frustum const &frustum,
    bool have_perspective_frustum,
    gpu_3do_parallel_visibility &out,
    bool accept_whole_mesh=false)
{
    out.ready=false;
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
            bool any_visible=false;
            for(uint32_t p=group.plane_start;
                p<group.plane_start+group.plane_count;++p) {
                auto const &plane=geo.face_planes[p];
                if(gpu_3do_face_facing_camera(
                    out.local_eye,plane.anchor,plane.normal)) {
                    any_visible=true;
                    break;
                }
            }
            if(!any_visible)reason=gpu_3do_cull_reason::backface;
        }
        out.groups[i]=reason;
    }
    out.ready=true;
}
} // namespace jkgm
