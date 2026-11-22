"""CPU reuse and state-boundary regression contracts for Hemi/Cube captures."""
from pathlib import Path
root=Path(__file__).resolve().parents[1]
read=lambda name:(root/name).read_text(encoding="utf-8")
capture=read("renderer/hemi_reflection_capture.inc")
renderer=read("renderer/renderer.cpp")
buckets=read("renderer/reflection_capture_world_buckets.hpp")
assert '#include "reflection_capture_world_buckets.hpp"' in renderer
assert "reflection_capture_world_buckets reflection_world_buckets;" in renderer
assert renderer.count("reflection_world_buckets.invalidate();")>=2
assert "reflection_world_buckets.refresh(" in capture
assert "reflection_world_buckets.buckets()" in capture
assert "cache.loaded_world,cache.revision,cache.mesh.surfaces" in capture
assert "std::map<std::tuple<uintptr_t,int32_t,bool>," not in capture
assert "surface_mask" in capture
assert "dual_paraboloid_bounds_outside(" in capture
assert "gpu_3do_batch_outside_frustum(" in capture
assert "std::vector<GLsizei> counts;" in capture
assert capture.index("std::vector<GLsizei> counts;") < capture.index(
    "for(int face=0;face<face_count;++face)")
assert "std::vector<gpu_3do_group_submission const *>" in capture
assert "inst.geometry->cached_group_submissions()" in capture
assert "model_group_scratch.push_back(&entry);" in capture
assert "entry->capture_keys[force_solid?1U:0U]" in capture
assert "group->index_offset_bytes" in capture
assert "inst.geometry->cached_group_bounds(mesh_min,mesh_max)" in capture
assert "whole_mesh_rejections" in capture
assert "group_walks_avoided" in capture
assert "std::map<reflection_capture_material_key,gpu_3do_texture>" in capture
assert "resolved_capture_materials.emplace(" in capture
assert "last_submitted_material" in capture
assert "std::make_pair(key,model)" in capture
assert "submit_material(mat,solid,material_key,true)" in capture
assert "submit_material(mat,solid,bucket.key,false)" in capture
assert capture.count("offscreen_capture_gl_scope restore;")==1
assert capture.index("offscreen_capture_gl_scope restore;") < capture.index(
    "for(int face=0;face<face_count;++face)")
assert "if(!probe_target->select_layer(face))" in capture
assert "glClear(GL_COLOR_BUFFER_BIT|GL_DEPTH_BUFFER_BIT)" in capture
assert "shader.use_program();" in capture
assert "glGenerateMipmap(GL_TEXTURE_2D_ARRAY)" in capture
assert "world_==world && revision_==revision" in buckets
assert "std::map<reflection_capture_material_key,std::vector<size_t>> sorted;" in buckets
assert "s.passable_adjoin" in buckets
assert "s.surface_flags&0x600U" in buckets
print("Reflection capture CPU reuse and visibility contracts OK")
