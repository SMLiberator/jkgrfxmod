"""Wiring contract for shared 3DO CPU snapshot cache protection and counters."""
from pathlib import Path
root=Path(__file__).resolve().parents[1]
read=lambda path:(root/path).read_text(encoding="utf-8")
main=read("renderer/main.cpp")
gpu=read("renderer/gpu_3do_preview.hpp")
frame=read("renderer/frame_profile.hpp")
renderer=read("renderer/renderer.cpp")
policy=read("renderer/gpu_mesh_cache_eviction.hpp")
assert '#include "gpu_mesh_cache_eviction.hpp"' in main
assert "gpu_mesh_cache.size()>4096" not in main
assert "gpu_mesh_cache.size()>=4096U" not in main
assert "gpu_mesh_cache.clear(); // level/renderer reset" in main
assert "gpu_mesh_cache_make_room();" in main
assert main.count("gpu_mesh_cache_make_room();")==2 # normal + planar
assert main.count("gpu_mesh_cache_inserted();")==2
assert main.count("last_used_frame=gpu_frame_serial")>=4
assert "gpu_mesh_cache_last_trim_frame==gpu_frame_serial" in main
assert "cache_entries=gpu_mesh_cache.size()" in main
assert "cache_peak=gpu_mesh_cache_peak" in main
assert "gpu_mesh_hash_reason" in main
for name in ("cache_hits","cache_misses","cache_evictions","cache_trim_deferred",
             "cache_hash_new","cache_hash_header","cache_hash_revision",
             "cache_hash_audit","cache_hash_fallback"):
    assert name in gpu
    assert name in main
    assert "gpu_"+name in frame
    assert "stats."+name in renderer
assert "PERF gpu_3do mesh_cache entries_avg=" in renderer
assert "PERF gpu_3do hash_causes_total new=" in renderer
assert "gpu_uploads" in renderer
assert "gpu_mesh_cache_protected_frames=128U" in policy
assert "result.evicted+=cache.erase(item.second)" in policy
assert "result.deferred=result.remaining>=limit" in policy
assert "mesh_revision::report_untracked()" in main
assert "mesh_revision::current()" in main
print("GPU 3DO cache eviction and profiling wiring OK")
