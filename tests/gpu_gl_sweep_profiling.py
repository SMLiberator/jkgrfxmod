"""Source contracts: GL cache sweep diagnostics do not alter expiration."""
from pathlib import Path
renderer=(Path(__file__).resolve().parents[1]/"renderer/renderer.cpp").read_text(encoding="utf-8")
assert "struct gpu_gl_sweep_profile {" in renderer
assert "std::unordered_map<uint64_t,uint64_t> gpu_gl_recent_swept_ids;" in renderer
sweep=renderer.split("// Transient 3DOs (text, HUD, effects)",1)[1].split("// Reset state:",1)[0]
assert "(gpu_3do_present_serial%16U)==0U" in sweep
assert "gpu_3do_present_serial-used->second>64U" in sweep
assert "bool const profile_sweep=the_config->profile_renderer;" in sweep
assert "if(profile_sweep)++scanned;" in sweep
assert "++s.no_timestamp;" in sweep
assert "++s.expired;" in sweep
assert "mesh.vertex_count*" in sweep
assert "sizeof(gpu_3do_packed_vertex)" in sweep
assert "sizeof(gpu_3do_vertex)" in sweep
assert "uint64_t(mesh.num_indices)*sizeof(uint32_t)" in sweep
assert "gpu_gl_recent_swept_ids[it->first]=" in sweep
assert "if(age>256U)" in sweep
assert "gpu_3do_gl_cache.find(it->first)" in sweep
assert "++gpu_gl_sweep_window.reappeared;" in sweep
assert "++gpu_gl_sweep_window.reappeared_within_64;" in sweep
assert "s.cpu_ns+=sweep_ns;" in sweep
assert "s.peak_ns=std::max(s.peak_ns,sweep_ns);" in sweep
assert "gpu_3do_last_drawn.erase(used);" in sweep
assert 'LOG_INFO("PERF gpu_gl_sweep sweeps=",' in renderer
assert 'LOG_INFO("PERF gpu_gl_sweep_reuse reappeared_within_256_frames=",' in renderer
assert "gpu_gl_sweep_window={};" in renderer
assert "gpu_gl_recent_swept_ids.clear();" in renderer
assert "if(!inst.transient_procedural)continue;" in renderer
assert "gpu_3do_gl_cache.erase(inst.id);" in renderer
print("GPU GL cache sweep telemetry, expiry parity and reuse tracking OK")
