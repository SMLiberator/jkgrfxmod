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
# Offscreen acquisition must honor the SAME generation/audit policy as
# ordinary GPU 3DOs, not rehash every native rdMesh every frame.
offscreen=loader.split("jkgm::acquire_planar_reflection_offscreen_gpu(",1)[1].split(
    "jkgm::gpu_3do_native_stats jkgm::consume_gpu_3do_native_stats()",1)[0]
for needle in ("bool const header_unchanged=",
               "bool const revision_fastpath=mesh_revision::active()",
               "generation=mesh_revision::current()",
               "gpu_frame_serial%32U",
               "source_checked_generation!=generation",
               "last_audit_frame!=gpu_frame_serial",
               "source_checked_frame!=gpu_frame_serial",
               "if(should_hash)",
               "mesh_revision::report_untracked()",
               "it->second.source_checked_frame=gpu_frame_serial",
               "entry.last_audit_frame=audit_frame ? gpu_frame_serial : 0U"):
    assert needle in offscreen,needle
assert "source_hashes" in header and "source_hash_skips" in header
assert "snapshot_gpu_mesh(" in loader
assert "gpu_mesh_materials_ready(" in loader
assert "source.world_transform" in loader
assert "instance.shadow_owner=source.owner" in loader
assert 'experimental_planar_thing_discovery &&' in renderer
assert "sample_budget=16U" in renderer
assert "size_t const remaining=scan.models.size();" in renderer
assert "acquire_planar_reflection_offscreen_gpu(" in renderer
assert "NOTE=offscreen_acquisition_only_no_capture_yet" in renderer
for project in ("renderer/renderer.vcxproj","renderer/renderer.vcxproj.filters"):
    assert "planar_reflection_native_nodes.hpp" in read(project)
    assert "planar_reflection_offscreen_gpu.hpp" in read(project)
assert "not yet drawn" in doc
print("Planar reflection native 3DO geometry and current pose contracts OK")
