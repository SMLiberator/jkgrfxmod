#pragma once
// Optional CPU submission timings: NOT GPU elapsed time.
#include <algorithm>
#include <array>
#include <cstddef>
#include <chrono>
#include <cstdint>
namespace jkgm {
enum class profile_stage : size_t {
  execute, mat_resolve, present_swap, hud_upload, sort_opaque,
  sort_transparent, upload, opaque_pass, transparent_pass, hud_draw,
  present_resolve, present_blur, present_retro, present_composite,
  present_swapbuffers, present_beginframe, count
};
using profile_clock = std::chrono::steady_clock;
constexpr size_t profile_stage_count = static_cast<size_t>(profile_stage::count);
inline size_t profile_idx(profile_stage s) { return static_cast<size_t>(s); }
struct profile_sample {
  std::array<uint64_t, profile_stage_count> timings{};
  uint64_t period_us=0, present_us=0, tris=0, trans_tris=0, draw_batches=0, binds=0;
  uint64_t sort_fallback_groups=0, sort_fallback_triangles=0, sort_fallback_frames=0;
  // Time outside the replacement renderer, measured at JK's Execute/Flip boundary.
  // These intervals also include any simulation/COG/driver work, not just 3DO processing.
  uint64_t before_first_execute_us=0, between_executes_us=0, after_last_execute_us=0;
  uint64_t execute_calls=0, execute_vertices=0, execute_instruction_bytes=0;
  uint64_t frames_with_execute=0;
  uint64_t native_model_calls=0, native_model_drawn=0, native_mesh_calls=0;
  uint64_t native_model_lod0_forced=0;
  uint64_t native_model_occlusion_skipped=0;
  uint64_t native_occlusion_tests=0,native_occlusion_lookups=0;
  uint64_t native_occlusion_lookup_hits=0,native_occlusion_ns=0;
  uint64_t native_face_calls=0, native_face_emitted=0;
  uint64_t native_model_ns=0, native_mesh_ns=0, native_face_ns=0;
  uint64_t native_model_hook_ns=0,native_model_engine_ns=0;
  uint64_t native_model_engine_self_ns=0;
  uint64_t native_mesh_queue_ns=0,native_mesh_native_ns=0,native_mesh_decal_ns=0;
  uint64_t native_static_decal_capture_calls=0,native_static_decal_capture_skipped=0;
  uint64_t native_mesh_camera_reads=0;
  uint64_t native_audit_models=0;
  uint64_t native_audit_matched=0;
  uint64_t native_audit_unavailable=0;
  uint64_t native_audit_rejected=0;
  uint64_t native_audit_callbacks=0;
  uint64_t native_audit_order=0;
  uint64_t native_audit_matrix=0;
  uint64_t native_audit_modes=0;
  uint64_t native_audit_geoset=0;
  uint64_t native_audit_result=0;
  uint64_t native_audit_nested=0;
  uint64_t native_direct_attempts=0;
  uint64_t native_direct_drawn=0;
  uint64_t native_direct_rejected=0;
  uint64_t native_direct_fallbacks=0;
  uint64_t native_direct_meshes=0;
  uint64_t native_direct_topology_hits=0;
  uint64_t native_direct_topology_misses=0;
  uint64_t native_direct_pose_refreshes=0;
  uint64_t native_direct_rollbacks=0;
  uint64_t native_direct_queue_ns=0;
  uint64_t native_prepared_hits=0;
  uint64_t native_prepared_misses=0;
  uint64_t native_prepared_empty_hits=0;
  uint64_t native_mesh_gpu_calls=0,native_mesh_native_calls=0;
  std::array<uint64_t,5> sprite_type_calls{};
  uint64_t sprite_other_type_calls=0,sprite_hidden_calls=0;
  uint64_t sprite_native_calls=0,sprite_native_success=0,sprite_native_ns=0;
  uint64_t sprite_native_proc_faces=0;
  uint64_t sprite_projected_gpu=0,sprite_projected_warmups=0;
    uint64_t sprite_gpu_cached_quads=0,sprite_gpu_quad_reuses=0;
    uint64_t sprite_gpu_quad_fallbacks=0,sprite_gpu_quad_mat_wait=0;
    std::array<uint64_t,8> sprite_gpu_quad_rejects{};
  uint64_t gpu_candidates=0,gpu_culled=0,gpu_queued=0,gpu_snapshots=0,gpu_ready_checks=0;
  uint64_t gpu_source_checks=0,gpu_source_ns=0,gpu_source_invalidations=0;
  uint64_t gpu_cache_entries=0,gpu_cache_peak=0;
  uint64_t gpu_cache_hits=0,gpu_cache_misses=0;
  uint64_t gpu_cache_evictions=0,gpu_cache_trim_deferred=0;
  uint64_t gpu_cache_hash_new=0,gpu_cache_hash_header=0;
  uint64_t gpu_cache_hash_revision=0;
  uint64_t gpu_cache_hash_fallback=0;
  uint64_t gpu_mutation_scans=0,gpu_mutation_scanned=0;
  uint64_t gpu_mutation_invalidated=0,gpu_mutation_ns=0;
  uint64_t gpu_failed_snapshot_reuses=0;
  uint64_t gpu_geometry_native=0,gpu_snapshot_native=0;
  uint64_t gpu_empty_mesh_native=0;
  uint64_t gpu_native_span_cache_hits=0,gpu_native_span_queries=0;
  uint64_t gpu_material_native=0,gpu_lighting_native=0;
  uint64_t gpu_ready_material_refs=0,gpu_ready_ns=0,gpu_ready_cache_hits=0;
  uint64_t gpu_draw_calls=0,gpu_draw_ranges=0,gpu_multidraw_calls=0;
  // Source ranges versus actual GL-index subdraws; instanced sprite
  // batches count as one subdraw and are excluded from merge savings.
  uint64_t gpu_index_subdraws=0,gpu_index_coalesced_ranges=0;
  uint64_t gpu_index_coalesced_singles=0;
  // Profile-only, sampled candidate counts for cross-Thing 3DO instancing.
  // Conservative candidates are a subset of repeated live GPU identities.
  uint64_t gpu_instancing_audit_samples=0;
  // Per-frame opaque instancing CPU work, separately from sampled audit.
  uint64_t gpu_instance_buffer_bytes=0,gpu_instance_buffer_uploads=0,gpu_instance_buffer_draws=0;
  uint64_t gpu_collection_layout_hits=0,gpu_collection_layout_misses=0,gpu_collection_direct_members=0;
  uint64_t gpu_specialized_collection_meshes=0,gpu_specialized_collection_groups=0;
  uint64_t gpu_simd_backface_groups=0,gpu_simd_backface_uncertain=0;
  uint64_t gpu_instancing_prepare_us=0,gpu_instancing_audit_us=0;
  uint64_t gpu_instancing_schedule_us=0,gpu_instancing_reorder_us=0;
  uint64_t gpu_instancing_source_draws=0;
  uint64_t gpu_instancing_geometry_duplicates=0;
  uint64_t gpu_instancing_geometry_groups=0;
  uint64_t gpu_instancing_conservative_draws=0;
  uint64_t gpu_instancing_compatible_duplicates=0;
  uint64_t gpu_instancing_compatible_groups=0;
  uint64_t gpu_instancing_repeated_member_ranges=0;
  uint64_t gpu_instancing_repeated_eligible_ranges=0;
  uint64_t gpu_instancing_rejected_ranges=0;
  uint64_t gpu_instancing_repeated_rejected_ranges=0;
  uint64_t gpu_instancing_eligible_repeat_extras=0;
  uint64_t gpu_instancing_state_split_extras=0;
  // One slot per bit in the audit's 32-bit mask. The audit header names
  // active reason slots without including heavy 3DO types here.
  std::array<uint64_t,32> gpu_instancing_rejects_all{};
  std::array<uint64_t,32> gpu_instancing_rejects_repeated{};
  // Sampled opaque-pass policies: solid/mixed/masked/blend_only/dithered.
  std::array<uint64_t,5> gpu_instancing_alpha_policies{};
  uint64_t gpu_3do_instanced_calls=0,gpu_3do_instanced_ranges=0;
  uint64_t gpu_sprite_instanced_calls=0,gpu_sprite_instanced_quads=0;
  uint64_t gpu_sprite_instancing_eligible=0;
  uint64_t gpu_opaque_draw_calls=0,gpu_transparent_draw_calls=0;
  uint64_t gpu_opaque_draw_ranges=0,gpu_transparent_draw_ranges=0;
  uint64_t gpu_draw_triangles=0,gpu_material_resolves=0,gpu_uploads=0;
  // Subrange counts, not GL calls or measured pixel overdraw. They describe
  // the GPU-resident draw workload without changing transparency order.
  uint64_t gpu_alpha_fluid_sprite_ranges=0;
  uint64_t gpu_alpha_other_sprite_ranges=0;
  uint64_t gpu_alpha_model_ranges=0;
  uint64_t gpu_opaque_cutout_sprite_ranges=0;
  // Visible 3DO material filter state: a "skip" avoids three glTexParameter
  // calls, tracked per pass using actual texture object IDs.
  uint64_t gpu_filter_state_sets=0,gpu_filter_state_skips=0;
  uint64_t gpu_groups_considered=0,gpu_groups_frustum_culled=0,gpu_groups_backface_culled=0;
  uint64_t gpu_mesh_frustum_accepted=0,gpu_group_frustum_tests_avoided=0;
  // Wall-clock CPU visibility prepass: two persistent helpers + render lane.
  // Count only actual worker dispatches, not frames below the threshold.
  uint64_t gpu_parallel_visibility_calls=0;
  uint64_t gpu_parallel_visibility_instances=0;
  uint64_t gpu_parallel_visibility_groups=0;
  uint64_t gpu_parallel_visibility_wall_us=0;
  uint64_t gpu_parallel_visibility_failures=0;
  uint64_t gpu_preview_cpu_us=0;
  // Separate opaque/transparent visible 3DO CPU work. These are subsets
  // of gpu_preview_cpu_us (which also includes shader/pipeline setup).
  std::array<uint64_t,2> gpu_preview_collect_us{};
  std::array<uint64_t,2> gpu_preview_sort_us{};
  std::array<uint64_t,2> gpu_preview_submit_us{};
  std::array<uint64_t,2> gpu_preview_calls{};
  void add(profile_sample const &o) {
    for(size_t i=0;i<timings.size();++i) timings[i]+=o.timings[i];
    period_us+=o.period_us; present_us+=o.present_us; tris+=o.tris;
    trans_tris+=o.trans_tris; draw_batches+=o.draw_batches; binds+=o.binds;
    sort_fallback_groups+=o.sort_fallback_groups;
    sort_fallback_triangles+=o.sort_fallback_triangles;
    sort_fallback_frames+=o.sort_fallback_frames;
    before_first_execute_us+=o.before_first_execute_us;
    between_executes_us+=o.between_executes_us;
    after_last_execute_us+=o.after_last_execute_us;
    execute_calls+=o.execute_calls;
    execute_vertices+=o.execute_vertices;
    execute_instruction_bytes+=o.execute_instruction_bytes;
    frames_with_execute+=o.frames_with_execute;
    native_model_calls+=o.native_model_calls;
    native_model_drawn+=o.native_model_drawn;
    native_model_lod0_forced+=o.native_model_lod0_forced;
    native_model_occlusion_skipped+=o.native_model_occlusion_skipped;
    native_occlusion_tests+=o.native_occlusion_tests;
    native_occlusion_lookups+=o.native_occlusion_lookups;
    native_occlusion_lookup_hits+=o.native_occlusion_lookup_hits;
    native_occlusion_ns+=o.native_occlusion_ns;
    native_mesh_calls+=o.native_mesh_calls;
    native_face_calls+=o.native_face_calls;
    native_face_emitted+=o.native_face_emitted;
    native_model_ns+=o.native_model_ns;
    native_mesh_ns+=o.native_mesh_ns;
    native_face_ns+=o.native_face_ns;
    native_model_hook_ns+=o.native_model_hook_ns;
    native_model_engine_ns+=o.native_model_engine_ns;
    native_model_engine_self_ns+=o.native_model_engine_self_ns;
    native_mesh_queue_ns+=o.native_mesh_queue_ns;
    native_mesh_native_ns+=o.native_mesh_native_ns;
    native_mesh_decal_ns+=o.native_mesh_decal_ns;
    native_static_decal_capture_calls+=o.native_static_decal_capture_calls;
    native_static_decal_capture_skipped+=o.native_static_decal_capture_skipped;
    native_mesh_camera_reads+=o.native_mesh_camera_reads;
    native_audit_models+=o.native_audit_models;
    native_audit_matched+=o.native_audit_matched;
    native_audit_unavailable+=o.native_audit_unavailable;
    native_audit_rejected+=o.native_audit_rejected;
    native_audit_callbacks+=o.native_audit_callbacks;
    native_audit_order+=o.native_audit_order;
    native_audit_matrix+=o.native_audit_matrix;
    native_audit_modes+=o.native_audit_modes;
    native_audit_geoset+=o.native_audit_geoset;
    native_audit_result+=o.native_audit_result;
    native_audit_nested+=o.native_audit_nested;
    native_direct_attempts+=o.native_direct_attempts;
    native_direct_drawn+=o.native_direct_drawn;
    native_direct_rejected+=o.native_direct_rejected;
    native_direct_fallbacks+=o.native_direct_fallbacks;
    native_direct_meshes+=o.native_direct_meshes;
    native_direct_topology_hits+=o.native_direct_topology_hits;
    native_direct_topology_misses+=o.native_direct_topology_misses;
    native_direct_pose_refreshes+=o.native_direct_pose_refreshes;
    native_direct_rollbacks+=o.native_direct_rollbacks;
    native_direct_queue_ns+=o.native_direct_queue_ns;
    native_prepared_hits+=o.native_prepared_hits;
    native_prepared_misses+=o.native_prepared_misses;
    native_prepared_empty_hits+=o.native_prepared_empty_hits;
    native_mesh_gpu_calls+=o.native_mesh_gpu_calls;
    native_mesh_native_calls+=o.native_mesh_native_calls;
    for(size_t i=0;i<sprite_type_calls.size();++i)
      sprite_type_calls[i]+=o.sprite_type_calls[i];
    sprite_other_type_calls+=o.sprite_other_type_calls;
    sprite_hidden_calls+=o.sprite_hidden_calls;
    sprite_native_calls+=o.sprite_native_calls;
    sprite_native_success+=o.sprite_native_success;
    sprite_native_ns+=o.sprite_native_ns;
    sprite_native_proc_faces+=o.sprite_native_proc_faces;
    sprite_projected_gpu+=o.sprite_projected_gpu;
    sprite_projected_warmups+=o.sprite_projected_warmups;
    sprite_gpu_cached_quads+=o.sprite_gpu_cached_quads;
    sprite_gpu_quad_reuses+=o.sprite_gpu_quad_reuses;
    sprite_gpu_quad_fallbacks+=o.sprite_gpu_quad_fallbacks;
    sprite_gpu_quad_mat_wait+=o.sprite_gpu_quad_mat_wait;
    for(size_t i=0;i<sprite_gpu_quad_rejects.size();++i)
        sprite_gpu_quad_rejects[i]+=o.sprite_gpu_quad_rejects[i];
    gpu_candidates+=o.gpu_candidates; gpu_culled+=o.gpu_culled;
    gpu_queued+=o.gpu_queued; gpu_snapshots+=o.gpu_snapshots;
    gpu_source_checks+=o.gpu_source_checks;
    gpu_source_ns+=o.gpu_source_ns;
    gpu_source_invalidations+=o.gpu_source_invalidations;
    gpu_cache_entries+=o.gpu_cache_entries;
    gpu_cache_peak=(std::max)(gpu_cache_peak,o.gpu_cache_peak);
    gpu_cache_hits+=o.gpu_cache_hits;
    gpu_cache_misses+=o.gpu_cache_misses;
    gpu_cache_evictions+=o.gpu_cache_evictions;
    gpu_cache_trim_deferred+=o.gpu_cache_trim_deferred;
    gpu_cache_hash_new+=o.gpu_cache_hash_new;
    gpu_cache_hash_header+=o.gpu_cache_hash_header;
    gpu_cache_hash_revision+=o.gpu_cache_hash_revision;
    gpu_cache_hash_fallback+=o.gpu_cache_hash_fallback;
    gpu_mutation_scans+=o.gpu_mutation_scans;
    gpu_mutation_scanned+=o.gpu_mutation_scanned;
    gpu_mutation_invalidated+=o.gpu_mutation_invalidated;
    gpu_mutation_ns+=o.gpu_mutation_ns;
    gpu_failed_snapshot_reuses+=o.gpu_failed_snapshot_reuses;
    gpu_empty_mesh_native+=o.gpu_empty_mesh_native;
    gpu_native_span_cache_hits+=o.gpu_native_span_cache_hits;
    gpu_native_span_queries+=o.gpu_native_span_queries;
    gpu_geometry_native+=o.gpu_geometry_native;
    gpu_snapshot_native+=o.gpu_snapshot_native;
    gpu_material_native+=o.gpu_material_native;
    gpu_lighting_native+=o.gpu_lighting_native;
    gpu_ready_material_refs+=o.gpu_ready_material_refs;
    gpu_ready_ns+=o.gpu_ready_ns;
    gpu_ready_cache_hits+=o.gpu_ready_cache_hits;
    gpu_ready_checks+=o.gpu_ready_checks; gpu_draw_calls+=o.gpu_draw_calls;
    gpu_draw_ranges+=o.gpu_draw_ranges;
    gpu_multidraw_calls+=o.gpu_multidraw_calls;
    gpu_index_subdraws+=o.gpu_index_subdraws;
    gpu_index_coalesced_ranges+=o.gpu_index_coalesced_ranges;
    gpu_index_coalesced_singles+=o.gpu_index_coalesced_singles;
    gpu_instancing_audit_samples+=o.gpu_instancing_audit_samples;
    gpu_collection_layout_hits+=o.gpu_collection_layout_hits;
    gpu_collection_layout_misses+=o.gpu_collection_layout_misses;
    gpu_collection_direct_members+=o.gpu_collection_direct_members;
    gpu_specialized_collection_meshes+=o.gpu_specialized_collection_meshes;
    gpu_specialized_collection_groups+=o.gpu_specialized_collection_groups;
    gpu_simd_backface_groups+=o.gpu_simd_backface_groups;
    gpu_simd_backface_uncertain+=o.gpu_simd_backface_uncertain;
    gpu_instance_buffer_bytes+=o.gpu_instance_buffer_bytes;
    gpu_instance_buffer_uploads+=o.gpu_instance_buffer_uploads;
    gpu_instance_buffer_draws+=o.gpu_instance_buffer_draws;
    gpu_instancing_prepare_us+=o.gpu_instancing_prepare_us;
    gpu_instancing_audit_us+=o.gpu_instancing_audit_us;
    gpu_instancing_schedule_us+=o.gpu_instancing_schedule_us;
    gpu_instancing_reorder_us+=o.gpu_instancing_reorder_us;
    gpu_instancing_source_draws+=o.gpu_instancing_source_draws;
    gpu_instancing_geometry_duplicates+=o.gpu_instancing_geometry_duplicates;
    gpu_instancing_geometry_groups+=o.gpu_instancing_geometry_groups;
    gpu_instancing_conservative_draws+=o.gpu_instancing_conservative_draws;
    gpu_instancing_compatible_duplicates+=o.gpu_instancing_compatible_duplicates;
    gpu_instancing_compatible_groups+=o.gpu_instancing_compatible_groups;
    gpu_instancing_repeated_member_ranges+=o.gpu_instancing_repeated_member_ranges;
    gpu_instancing_repeated_eligible_ranges+=o.gpu_instancing_repeated_eligible_ranges;
    gpu_instancing_rejected_ranges+=o.gpu_instancing_rejected_ranges;
    gpu_instancing_repeated_rejected_ranges+=o.gpu_instancing_repeated_rejected_ranges;
    gpu_instancing_eligible_repeat_extras+=o.gpu_instancing_eligible_repeat_extras;
    gpu_instancing_state_split_extras+=o.gpu_instancing_state_split_extras;
    for(size_t i=0;i<gpu_instancing_rejects_all.size();++i) {
        gpu_instancing_rejects_all[i]+=o.gpu_instancing_rejects_all[i];
        gpu_instancing_rejects_repeated[i]+=
            o.gpu_instancing_rejects_repeated[i];
    }
    for(size_t i=0;i<gpu_instancing_alpha_policies.size();++i)
        gpu_instancing_alpha_policies[i]+=o.gpu_instancing_alpha_policies[i];
    gpu_3do_instanced_calls+=o.gpu_3do_instanced_calls;
    gpu_3do_instanced_ranges+=o.gpu_3do_instanced_ranges;
    gpu_sprite_instanced_calls+=o.gpu_sprite_instanced_calls;
    gpu_sprite_instanced_quads+=o.gpu_sprite_instanced_quads;
    gpu_sprite_instancing_eligible+=o.gpu_sprite_instancing_eligible;
    gpu_opaque_draw_calls+=o.gpu_opaque_draw_calls;
    gpu_transparent_draw_calls+=o.gpu_transparent_draw_calls;
    gpu_opaque_draw_ranges+=o.gpu_opaque_draw_ranges;
    gpu_transparent_draw_ranges+=o.gpu_transparent_draw_ranges;
    gpu_draw_triangles+=o.gpu_draw_triangles;
    gpu_alpha_fluid_sprite_ranges+=o.gpu_alpha_fluid_sprite_ranges;
    gpu_alpha_other_sprite_ranges+=o.gpu_alpha_other_sprite_ranges;
    gpu_alpha_model_ranges+=o.gpu_alpha_model_ranges;
    gpu_opaque_cutout_sprite_ranges+=o.gpu_opaque_cutout_sprite_ranges;
    gpu_material_resolves+=o.gpu_material_resolves;
    gpu_uploads+=o.gpu_uploads;
    gpu_filter_state_sets+=o.gpu_filter_state_sets;
    gpu_filter_state_skips+=o.gpu_filter_state_skips;
    gpu_groups_considered+=o.gpu_groups_considered;
    gpu_groups_frustum_culled+=o.gpu_groups_frustum_culled;
    gpu_mesh_frustum_accepted+=o.gpu_mesh_frustum_accepted;
    gpu_group_frustum_tests_avoided+=o.gpu_group_frustum_tests_avoided;
    gpu_groups_backface_culled+=o.gpu_groups_backface_culled;
    gpu_parallel_visibility_calls+=o.gpu_parallel_visibility_calls;
    gpu_parallel_visibility_instances+=o.gpu_parallel_visibility_instances;
    gpu_parallel_visibility_groups+=o.gpu_parallel_visibility_groups;
    gpu_parallel_visibility_wall_us+=o.gpu_parallel_visibility_wall_us;
    gpu_parallel_visibility_failures+=o.gpu_parallel_visibility_failures;
    gpu_preview_cpu_us+=o.gpu_preview_cpu_us;
    for(size_t i=0;i<2;++i) {
      gpu_preview_collect_us[i]+=o.gpu_preview_collect_us[i];
      gpu_preview_sort_us[i]+=o.gpu_preview_sort_us[i];
      gpu_preview_submit_us[i]+=o.gpu_preview_submit_us[i];
      gpu_preview_calls[i]+=o.gpu_preview_calls[i];
    }
  }
};
class profile_scope {
  uint64_t *counter;
  profile_clock::time_point begin;
public:
  profile_scope(bool enabled, uint64_t &out) : counter(enabled ? &out : nullptr) {
    if(counter) begin=profile_clock::now();
  }
  profile_scope(profile_scope const&)=delete;
  profile_scope& operator=(profile_scope const&)=delete;
  ~profile_scope() {
    if(counter) *counter+=static_cast<uint64_t>(
      std::chrono::duration_cast<std::chrono::microseconds>(profile_clock::now()-begin).count());
  }
};
}