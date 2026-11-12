#pragma once
// Read-only bridge between all-sector Thing discovery and GPU snapshots
// from JK's primary-camera traversal. This does NOT obtain missing meshes,
// draw to a reflection target, or infer a pose from a Thing's root position.
#include "planar_reflection_things.hpp"
#include "planar_reflection_camera.hpp"
#include "gpu_3do_preview.hpp"
#include <cstddef>
#include <cstdint>
#include <unordered_map>
#include <vector>

namespace jkgm {
    struct planar_reflection_cached_view {
        size_t instance_index=0;
        gpu_3do_matrix34 modelview{};
    };
    struct planar_reflection_workset {
        // Indices, not pointers: native captures and HUD deferral can move
        // the frame vector. A Thing may own several animated mesh nodes.
        std::vector<size_t> cached_instance_indices;
        // These Things are NOT necessarily outside the primary view:
        // native fallback, absent MATs and unsupported geometry also count.
        std::vector<planar_reflection_model_thing> uncovered_things;
        size_t matched_things=0;
        size_t unowned_gpu_nodes=0;
        size_t unmatched_gpu_nodes=0;
    };
    inline uint64_t planar_reflection_thing_owner_key(
        planar_reflection_model_thing const &thing)
    {
        // Matches the existing rdModel3 hook's shadow_owner encoding.
        return (uint64_t(uint32_t(thing.thing))<<32)|thing.signature;
    }
    inline planar_reflection_workset build_planar_reflection_workset(
        planar_reflection_thing_scan const &scan,
        std::vector<gpu_3do_instance> const &instances)
    {
        planar_reflection_workset out{};
        if(scan.error!=planar_reflection_thing_error::none)return out;
        std::unordered_map<uint64_t,size_t> owner_to_thing;
        owner_to_thing.reserve(scan.models.size());
        std::vector<uint8_t> covered(scan.models.size(),0U);
        for(size_t i=0;i<scan.models.size();++i)
            owner_to_thing.emplace(
                planar_reflection_thing_owner_key(scan.models[i]),i);
        for(size_t i=0;i<instances.size();++i) {
            auto const &node=instances[i];
            if(node.shadow_only || node.camera_hud ||
               node.is_projected_decal || node.is_sprite_quad ||
               !node.geometry || node.geometry->indices.empty() ||
               node.geometry->groups.empty() ||
               !gpu_3do_matrix_finite(node.transform))
                continue;
            if(node.shadow_owner==0U) {
                ++out.unowned_gpu_nodes;
                continue;
            }
            auto const found=owner_to_thing.find(node.shadow_owner);
            if(found==owner_to_thing.end()) {
                ++out.unmatched_gpu_nodes;
                continue;
            }
            out.cached_instance_indices.push_back(i);
            if(!covered[found->second]) {
                covered[found->second]=1U;
                ++out.matched_things;
            }
        }
        for(size_t i=0;i<scan.models.size();++i)
            if(!covered[i])out.uncovered_things.push_back(scan.models[i]);
        return out;
    }
    // Build independent model-view matrices without reading or modifying the
    // native rdCamera. World-space node transforms remain untouched.
    inline bool build_planar_reflection_cached_views(
        planar_reflection_workset const &work,
        std::vector<gpu_3do_instance> const &instances,
        engine_camera const &reflected_camera,
        std::vector<planar_reflection_cached_view> &out)
    {
        out.clear();
        if(!reflected_camera.valid)return false;
        auto const view=planar_camera_native_view(reflected_camera);
        out.reserve(work.cached_instance_indices.size());
        for(size_t const index:work.cached_instance_indices) {
            if(index>=instances.size() || !instances[index].geometry ||
               !gpu_3do_matrix_finite(instances[index].transform)) {
                out.clear();
                return false;
            }
            auto const m=gpu_3do_native_modelview(
                view,instances[index].transform);
            if(!gpu_3do_matrix_finite(m)) {
                out.clear();
                return false;
            }
            out.push_back({index,m});
        }
        return true;
    }
} // namespace jkgm
