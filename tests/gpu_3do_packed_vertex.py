"""Packed GPU vertex and shader wiring; legacy modes must remain intact."""
from pathlib import Path
import json
import re
root=Path(__file__).resolve().parents[1]
read=lambda path:(root/path).read_text(encoding="utf-8")
cfg=read("common/config.hpp")
parser=read("common/config.cpp")
sample=json.loads(read("assets/jkgm.json"))
docs=read("doc/config.md")
packed=read("renderer/gpu_3do_packed_vertex.hpp")
renderer=read("renderer/renderer.cpp")
vertex=read("assets/shaders/gpu_3do_preview.vert")
planar=read("assets/shaders/planar_capture.vert")
reflection=read("renderer/planar_reflection_capture_shader.hpp")
assert "bool experimental_packed_3do_vertices = false;" in cfg
assert 'j.contains("experimental_packed_3do_vertices")' in parser
assert sample["experimental_packed_3do_vertices"] is False
assert "| **experimental_packed_3do_vertices** |" in docs
assert "sizeof(gpu_3do_packed_vertex)==52" in packed
assert "sizeof(gpu_3do_vertex)==80" in read("renderer/gpu_3do_preview.hpp")
assert "gpu_3do_pack_vertices" in packed and "v.light_mode>3.0f" in packed
assert "std::fabs(length_squared-1.0f)" not in packed
assert "std::fabs(value-std::round(value*255.0f)" not in packed
assert "512.0f+(limited<0.0f" in packed
assert "decode_packed_normal(local_face_normal_mode.xyz)" in vertex
assert "decode_packed_normal(local_light_normal_input)" in vertex
assert "uint32_t(v.light_mode)" in packed
# Count the eight GPU mesh upload constructors, not unrelated config reads.
assert len(re.findall(
    r"std::make_unique<gpu_3do_gl_mesh>\(\s*"
    r"the_config->experimental_packed_3do_vertices\s*,", renderer))==8
assert renderer.count("&packed_3do_upload_profile : nullptr")==8
assert "PERF gpu_3do vertex_packing uploads_cumulative=" in renderer
assert "PERF gpu_3do packing_rejections empty=" in renderer
assert "PERF gpu_3do packing_live_cache packed=" in renderer
assert "gpu_3do_pack_vertex_rejections" in packed
assert "reason_buckets_overlap_one_mesh_can_have_multiple_reasons" in renderer
assert "gl::vertex_element_type::uint_2_10_10_10_rev" in renderer
for member in ("normal_mode","light_normal","rgba"):
    assert f"offsetof(gpu_3do_packed_vertex,{member})" in renderer
assert "prog.set_packed_vertex_format(it->second->packed_vertex_format)" in renderer
assert "shader.set_modelview(modelview,true," in renderer
assert "shader.set_modelview(draw.modelview,true," in renderer
assert "shader.set_modelview(batch.modelview,true," in renderer
assert "round(local_face_normal_mode.w*3.0)" in vertex
assert "pixel_light_state=vec3(local_static_light,local_extra_light,decoded_light_mode)" in vertex
assert "round(model_face_normal_mode.w*3.0)" in planar
assert "model && packed?1:0" in reflection
assert "gl::vertex_element_type::uint8" in renderer
assert "packed_vertex_format" in read("renderer/shaders.cpp")
assert "gpu_3do_packed_vertex.hpp" in read("renderer/renderer.vcxproj")
assert "gpu_3do_packed_vertex.hpp" in read("renderer/renderer.vcxproj.filters")
print("Packed GPU 3DO vertex contracts passed")
