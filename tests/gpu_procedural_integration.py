"""Guard direct GPU procedural drawing and forbid native-world material filtering."""
from pathlib import Path
root=Path(__file__).resolve().parents[1]
main=(root/"renderer/main.cpp").read_text(encoding="utf-8")
renderer=(root/"renderer/renderer.cpp").read_text(encoding="utf-8")
model=(root/"renderer/gpu_3do_preview.hpp").read_text(encoding="utf-8")
polyline=(root/"renderer/gpu_polyline.hpp").read_text(encoding="utf-8")
particle=(root/"renderer/gpu_particle_cloud.hpp").read_text(encoding="utf-8")
assert "HookedNativePolylineDraw" in main
assert "HookedNativePolylineFace" in main
assert "request_native_polyline_hooks()" in main
assert "native_procedural_draw::verified(read)" in main
assert "make_gpu_polyline_geometry(captured)" in main
assert "gpu_mesh_materials_ready(*geom" in main
assert "submit_gpu_particle_cloud(rdthing,orientation)" in main
assert "make_gpu_particle_cloud(dots)" in main
assert "gpu_mesh_materials_ready(*geometry" in main
assert "inst.transient_procedural=true;" in main
assert "gpu_3do_native_modelview(view,placement)" in main
assert "particle_texture_dimensions(" in main
assert "cel_dimensions.emplace(" in main
assert "isolate_native_world_batches()" in renderer
assert "native_world_filter" not in renderer
assert "gpu_3do_gl_cache.erase(inst.id)" in renderer
assert "transient_procedural" in model
assert "capture.faces.size()!=5U" in polyline
assert "group.centroid[n]+=p[n]" in polyline
assert "dot.radius*1.4142135623730951f" not in particle or "1.4142135623730951f" in particle
print("GPU procedural drawing source integration checks passed")
