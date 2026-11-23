#pragma once
// Immutable CPU visibility work for secondary reflection cameras.
// Workers may read captured 3DO geometry, transforms, camera matrices and
// portal masks; engine reads, material resolution and GL remain serial.
#include "gpu_3do_preview.hpp"
#include "gpu_3do_compat.hpp"
#include "gpu_3do_frustum.hpp"
#include "gpu_3do_modelview.hpp"
#include "dual_paraboloid_projection.hpp"
#include <array>
#include <cstdint>
#include <vector>

namespace jkgm {
enum class reflection_capture_cull : uint8_t {
    visible=0U,hemisphere=1U,range=2U,frustum=3U,portal=4U
};

// Use the exact tests and ordering of the serial Hemi/Cube/Planar paths.
// Radius is ONLY an explicit Hemi/Cube Thing limit; world and Planar use 0.
inline reflection_capture_cull reflection_capture_bounds_reason(
    gpu_3do_matrix34 const &modelview,
    gpu_3do_native_frustum const &frustum,
    bool paraboloid,float radius,
    std::array<float,3> const &lo,std::array<float,3> const &hi)
{
    if(paraboloid)
        return dual_paraboloid_bounds_outside(modelview,lo,hi,radius)
            ?reflection_capture_cull::hemisphere
            :reflection_capture_cull::visible;
    if(radius>0.0f &&
       reflection_probe_bounds_outside_range(modelview,lo,hi,radius))
        return reflection_capture_cull::range;
    if(gpu_3do_batch_outside_frustum(frustum,modelview,lo,hi))
        return reflection_capture_cull::frustum;
    return reflection_capture_cull::visible;
}

struct reflection_capture_parallel_visibility {
    gpu_3do_matrix34 modelview{};
    std::vector<reflection_capture_cull> groups; // native group indices
    reflection_capture_cull mesh=reflection_capture_cull::visible;
    bool ready=false;
};

inline void prepare_reflection_capture_parallel_visibility(
    gpu_3do_instance const &inst,
    gpu_3do_matrix34 const &view,
    gpu_3do_native_frustum const &frustum,
    bool paraboloid,float thing_radius,
    reflection_capture_parallel_visibility &out)
{
    out.ready=false;
    out.mesh=reflection_capture_cull::visible;
    if(!inst.geometry || !gpu_3do_matrix_finite(inst.transform))
        return; // fail open to the original serial collector
    out.modelview=gpu_3do_native_modelview(view,inst.transform);
    if(!gpu_3do_matrix_finite(out.modelview))return;
    auto const &geo=*inst.geometry;
    std::array<float,3> lo{},hi{};
    if(geo.cached_group_bounds(lo,hi)) {
        out.mesh=reflection_capture_bounds_reason(
            out.modelview,frustum,paraboloid,thing_radius,lo,hi);
        if(out.mesh!=reflection_capture_cull::visible) {
            out.groups.clear();
            out.ready=true;
            return;
        }
    }
    out.groups.resize(geo.groups.size());
    for(size_t i=0;i<geo.groups.size();++i) {
        auto const &group=geo.groups[i];
        out.groups[i]=reflection_capture_bounds_reason(
            out.modelview,frustum,paraboloid,thing_radius,
            group.bounds_min,group.bounds_max);
    }
    out.ready=true;
}
} // namespace jkgm
