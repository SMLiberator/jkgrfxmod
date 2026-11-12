"""Source contracts for independent offscreen animated rdModel3 acquisition."""
from pathlib import Path
root=Path(__file__).resolve().parents[1]
read=lambda p:(root/p).read_text(encoding="utf-8")
native=read("renderer/planar_reflection_native_nodes.hpp")
renderer=read("renderer/renderer.cpp")
loader=read("renderer/main.cpp")
header=read("renderer/planar_reflection_offscreen_gpu.hpp")
doc=read("doc/planar_reflections.md")
assert "collect_planar_reflection_native_nodes(" in native
assert "validate_native_thing_node_accessor(read)" in native
assert "read_sprite_decal_active_geoset(" in native
assert "precise_type3_read_pose_batch(" in native
assert "mots_native_detail::hierarchy_pointer_to_index(" in native
assert "source.signature" in native and "source.model" in native
assert "++occurrences[mesh]" in native
assert "out=std::move(found)" in native
assert "acquire_planar_reflection_offscreen_gpu(" in header
assert "acquire_planar_reflection_offscreen_gpu(" in loader
assert "gpu_mesh_source_signature(" in loader
assert "snapshot_gpu_mesh(" in loader
assert "gpu_mesh_materials_ready(" in loader
assert "source.world_transform" in loader
assert "instance.shadow_owner=source.owner" in loader
assert 'experimental_planar_thing_discovery &&' in renderer
assert "sample_budget=16U" in renderer
assert "acquire_planar_reflection_offscreen_gpu(" in renderer
assert "NOTE=offscreen_acquisition_only_no_capture_yet" in renderer
for project in ("renderer/renderer.vcxproj","renderer/renderer.vcxproj.filters"):
    assert "planar_reflection_native_nodes.hpp" in read(project)
    assert "planar_reflection_offscreen_gpu.hpp" in read(project)
assert "not yet drawn" in doc
print("Planar reflection native 3DO geometry and current pose contracts OK")
