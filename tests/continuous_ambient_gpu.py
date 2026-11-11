"""GPU-ready continuous ambient hash-table transport source contracts.

Stage B0 is CPU-side data packing only: no runtime shader behavior yet.
"""
from pathlib import Path
root=Path(__file__).resolve().parents[1]
cpu=(root/"renderer/continuous_ambient.hpp").read_text(encoding="utf-8")
packed=(root/"renderer/continuous_ambient_gpu.hpp").read_text(encoding="utf-8")
glsl=(root/"assets/shaders/continuous_ambient_lookup.glsl").read_text(encoding="utf-8")
cpp=(root/"tests/continuous_ambient_gpu.cpp").read_text(encoding="utf-8")
project=(root/"renderer/renderer.vcxproj").read_text(encoding="utf-8")
filters=(root/"renderer/renderer.vcxproj.filters").read_text(encoding="utf-8")
workflow=(root/".github/workflows/main.yml").read_text(encoding="utf-8")
renderer=(root/"renderer/renderer.cpp").read_text(encoding="utf-8")
vertex=(root/"assets/shaders/gpu_3do_preview.vert").read_text(encoding="utf-8")
assert "for_each_node(Visit &&visit) const" in cpu
assert "bool get_fallback(uint32_t sector" in cpu
assert "class continuous_ambient_gpu_table {" in packed
assert "std::vector<key> keys_" in packed
assert "std::vector<value> values_" in packed
assert "uint32_t empty_sector=0xffffffffU" in packed
assert "max_probe_budget=32U" in packed
assert "std::sort(entries.begin(),entries.end()," in packed
assert "if(fill(entries,capacity))" in packed
assert "if(++probes>=max_probe_budget)return false;" in packed
assert "while(keys_[slot][0]!=empty_sector)" in packed
assert "bool update_shared_floors(" in packed
assert "if(!floor(sector,live) || !std::isfinite(live))return false;" in packed
assert "values_[i][1]=updates[i];" in packed
assert "static_assert(sizeof(continuous_ambient_gpu_table::key)==16" in packed
assert "static_assert(sizeof(continuous_ambient_gpu_table::value)==8" in packed
assert "uniform usamplerBuffer continuous_voxel_keys;" in glsl
assert "uniform samplerBuffer continuous_voxel_values;" in glsl
assert "uniform uint continuous_voxel_mask;" in glsl
assert "texelFetch(continuous_voxel_keys,int(slot))" in glsl
assert "texelFetch(continuous_voxel_values,int(slot)).xy" in glsl
assert "slot=(slot+1u)&continuous_voxel_mask;" in glsl
assert "for(int attempt=0;attempt<32;++attempt)" in glsl
# Match exact CPU/GLSL wrapping constants and slot indexing:
for cpp_const,glsl_const in [
    ("2166136261U","2166136261u"),("16777619U","16777619u"),
    ("0x7feb352dU","0x7feb352du"),("0x846ca68bU","0x846ca68bu")]:
    assert cpp_const in packed and glsl_const in glsl
assert "packed.max_probes()<=table::max_probe_budget" in cpp
assert "packed.keys()==old_keys" in cpp
assert "packed.values()==old_values" in cpp
assert "source.node_count()>40000U" in cpp
assert "continuous_ambient_gpu.hpp" in project
assert "continuous_ambient_gpu.hpp" in filters
assert "GPU-ready continuous ambient sparse voxel packing" in workflow
assert "tests\\continuous_ambient_gpu.cpp" in workflow
assert "assets/shaders/continuous_ambient_lookup.glsl" not in vertex
assert "continuous_ambient_gpu.hpp" not in renderer
print("Continuous ambient sparse GPU transport contracts passed")
