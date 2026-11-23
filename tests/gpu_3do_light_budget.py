"""Too many dynamic lights must clip lighting, never reject cached meshes."""
from pathlib import Path
root=Path(__file__).resolve().parents[1]
main=(root/"renderer/main.cpp").read_text(encoding="utf-8")
header=(root/"renderer/gpu_3do_preview.hpp").read_text(encoding="utf-8")
shaders=(root/"renderer/shaders.cpp").read_text(encoding="utf-8")
workflow=(root/".github/workflows/main.yml").read_text(encoding="utf-8")
start=main.index("bool snapshot_gpu_3do_lighting(")
end=main.index("// Memoize readable virtual-memory regions",start)
light=main[start:end]
assert "select_gpu_3do_light_budget(" in light
assert "instance.num_lights=choice.count;" in light
assert "instance.lights[i]=candidates[choice.indices[i]]" in light
assert "gpu_native_stats.light_budget_clipped_meshes" in light
assert "gpu_native_stats.light_budget_dropped" in light
assert "return false; // Preserve native lighting rather than discard lights." not in main
assert "gpu_mesh_light_overflow" not in main
assert "lighting_capacity_fallback" not in main
assert light.count("return false;")==2, "Only an unreadable native camera may fall back"
assert "return true; // no inverse for light positions; keep the 3DO" in light
assert "if(type<0 || type>2)" in light
assert "++skipped;continue;" in light
assert "std::min(count,gpu_3do_max_lights)" in header
assert "constexpr unsigned gpu_3do_max_lights=16;" in header
assert "for(unsigned i=0;i<instance.num_lights;i++)" in shaders
assert "GPU 3DO light budget clipping" in workflow
assert "gpu_3do_light_budget.cpp" in workflow
print("Dynamic light count cannot reject cached 3DOs; bounded selection verified")
