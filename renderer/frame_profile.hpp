#pragma once
// Optional CPU submission timings: NOT GPU elapsed time.
#include <array>
#include <cstddef>
#include <chrono>
#include <cstdint>
namespace jkgm {
enum class profile_stage : size_t {
  execute, mat_resolve, present_swap, hud_upload, sort_opaque,
  sort_transparent, upload, opaque_pass, transparent_pass, hud_draw, count
};
using profile_clock = std::chrono::steady_clock;
constexpr size_t profile_stage_count = static_cast<size_t>(profile_stage::count);
inline size_t profile_idx(profile_stage s) { return static_cast<size_t>(s); }
struct profile_sample {
  std::array<uint64_t, profile_stage_count> timings{};
  uint64_t period_us=0, present_us=0, tris=0, trans_tris=0, draw_batches=0, binds=0;
  // Time outside the replacement renderer, measured at JK's Execute/Flip boundary.
  // These intervals also include any simulation/COG/driver work, not just 3DO processing.
  uint64_t before_first_execute_us=0, between_executes_us=0, after_last_execute_us=0;
  uint64_t execute_calls=0, execute_vertices=0, execute_instruction_bytes=0;
  uint64_t frames_with_execute=0;
  uint64_t native_model_calls=0, native_model_drawn=0, native_mesh_calls=0;
  uint64_t native_face_calls=0, native_face_emitted=0;
  uint64_t native_model_ns=0, native_mesh_ns=0, native_face_ns=0;
  uint64_t gpu_candidates=0,gpu_culled=0,gpu_queued=0,gpu_snapshots=0,gpu_ready_checks=0;
  uint64_t gpu_draw_calls=0,gpu_draw_triangles=0,gpu_material_resolves=0,gpu_uploads=0;
  uint64_t gpu_preview_cpu_us=0;
  void add(profile_sample const &o) {
    for(size_t i=0;i<timings.size();++i) timings[i]+=o.timings[i];
    period_us+=o.period_us; present_us+=o.present_us; tris+=o.tris;
    trans_tris+=o.trans_tris; draw_batches+=o.draw_batches; binds+=o.binds;
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
    gpu_candidates+=o.gpu_candidates; gpu_culled+=o.gpu_culled;
    gpu_queued+=o.gpu_queued; gpu_snapshots+=o.gpu_snapshots;
    gpu_ready_checks+=o.gpu_ready_checks; gpu_draw_calls+=o.gpu_draw_calls;
    gpu_draw_triangles+=o.gpu_draw_triangles;
    gpu_material_resolves+=o.gpu_material_resolves;
    gpu_uploads+=o.gpu_uploads;
    gpu_preview_cpu_us+=o.gpu_preview_cpu_us;
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