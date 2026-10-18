#pragma once
// Optional CPU submission timings: NOT GPU elapsed time.
#include <array>
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
  void add(profile_sample const &o) {
    for(size_t i=0;i<timings.size();++i) timings[i]+=o.timings[i];
    period_us+=o.period_us; present_us+=o.present_us; tris+=o.tris;
    trans_tris+=o.trans_tris; draw_batches+=o.draw_batches; binds+=o.binds;
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