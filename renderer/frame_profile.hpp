#pragma once
// Optional CPU submission timings: NOT GPU elapsed time.
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
  uint64_t native_face_calls=0, native_face_emitted=0;
  uint64_t native_model_ns=0, native_mesh_ns=0, native_face_ns=0;
  uint64_t native_model_hook_ns=0,native_model_engine_ns=0;
  uint64_t native_model_engine_self_ns=0;
  uint64_t native_mesh_queue_ns=0,native_mesh_native_ns=0,native_mesh_decal_ns=0;
  uint64_t native_mesh_gpu_calls=0,native_mesh_native_calls=0;
  std::array<uint64_t,5> sprite_type_calls{};
  uint64_t sprite_other_type_calls=0,sprite_hidden_calls=0;
  uint64_t sprite_native_calls=0,sprite_native_success=0,sprite_native_ns=0;
  uint64_t sprite_native_proc_faces=0;
  uint64_t sprite_projected_gpu=0,sprite_projected_warmups=0;
    uint64_t sprite_gpu_cached_quads=0,sprite_gpu_quad_reuses=0;
    uint64_t sprite_gpu_quad_fallbacks=0,sprite_gpu_quad_mat_wait=0;
  uint64_t gpu_candidates=0,gpu_culled=0,gpu_queued=0,gpu_snapshots=0,gpu_ready_checks=0;
  uint64_t gpu_source_checks=0,gpu_source_ns=0,gpu_source_invalidations=0;
  uint64_t gpu_failed_snapshot_reuses=0;
  uint64_t gpu_geometry_native=0,gpu_snapshot_native=0;
  uint64_t gpu_empty_mesh_native=0;
  uint64_t gpu_native_span_cache_hits=0,gpu_native_span_queries=0;
  uint64_t gpu_material_native=0,gpu_lighting_native=0;
  uint64_t gpu_ready_material_refs=0,gpu_ready_ns=0,gpu_ready_cache_hits=0;
  uint64_t gpu_draw_calls=0,gpu_draw_ranges=0,gpu_multidraw_calls=0;
  uint64_t gpu_opaque_draw_calls=0,gpu_transparent_draw_calls=0;
  uint64_t gpu_opaque_draw_ranges=0,gpu_transparent_draw_ranges=0;
  uint64_t gpu_draw_triangles=0,gpu_material_resolves=0,gpu_uploads=0;
  // Visible 3DO material filter state: a "skip" avoids three glTexParameter
  // calls, tracked per pass using actual texture object IDs.
  uint64_t gpu_filter_state_sets=0,gpu_filter_state_skips=0;
  uint64_t gpu_groups_considered=0,gpu_groups_frustum_culled=0,gpu_groups_backface_culled=0;
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
    gpu_candidates+=o.gpu_candidates; gpu_culled+=o.gpu_culled;
    gpu_queued+=o.gpu_queued; gpu_snapshots+=o.gpu_snapshots;
    gpu_source_checks+=o.gpu_source_checks;
    gpu_source_ns+=o.gpu_source_ns;
    gpu_source_invalidations+=o.gpu_source_invalidations;
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
    gpu_opaque_draw_calls+=o.gpu_opaque_draw_calls;
    gpu_transparent_draw_calls+=o.gpu_transparent_draw_calls;
    gpu_opaque_draw_ranges+=o.gpu_opaque_draw_ranges;
    gpu_transparent_draw_ranges+=o.gpu_transparent_draw_ranges;
    gpu_draw_triangles+=o.gpu_draw_triangles;
    gpu_material_resolves+=o.gpu_material_resolves;
    gpu_uploads+=o.gpu_uploads;
    gpu_filter_state_sets+=o.gpu_filter_state_sets;
    gpu_filter_state_skips+=o.gpu_filter_state_skips;
    gpu_groups_considered+=o.gpu_groups_considered;
    gpu_groups_frustum_culled+=o.gpu_groups_frustum_culled;
    gpu_groups_backface_culled+=o.gpu_groups_backface_culled;
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