"""The 3DO instancing feasibility pass must not batch CloneModel filenames."""
from pathlib import Path
root=Path(__file__).resolve().parents[1]
read=lambda name:(root/name).read_text(encoding="utf-8")
renderer=read("renderer/renderer.cpp")
header=read("renderer/gpu_3do_instancing_audit.hpp")
profile=read("renderer/frame_profile.hpp")
workflow=read(".github/workflows/main.yml")
audit=renderer.split("void draw_gpu_3do_preview(",1)[1].split(
    "// Deliberate world-cache isolation test.",1)[0]
assert '#include "gpu_3do_instancing_audit.hpp"' in renderer
assert "gpu_3do_present_serial%30U==0U" in audit
assert "bool const sample_instancing=gpu_profiling && prepare_both &&" in audit
assert "audit_gpu_3do_instancing(records)" in audit
assert "gpu_3do_instancing_conservative_candidate(" in audit
assert "gpu_3do_instancing_capture_state(" in audit
assert "inst.geometry.get()" in audit
assert "draw.group" in audit
assert "draw.texture" in audit
assert "record.mesh_id=inst.id;" in audit
assert "material.has_environment" in audit
assert "material.has_matcap" in audit
assert "material.has_normal" in audit
assert "specular_view_dependent" in audit
assert "std::map<geometry_key,std::set<uintptr_t>> geometric;" in header
assert "std::map<compatible_key,std::set<uintptr_t>> compatible;" in header
assert "r.mesh_id,r.geometry,r.group,r.material" in header
assert "compatible[{identity,r.state}]" in header
assert "inst.num_lights==0U" in header
assert "policy==gpu_3do_alpha_mode::solid" in header
assert "inst.thing_opacity==1.0f" in header
assert "inst.num_lights==0U && !inst.crossing_lighting" in header
assert "profile_frame.gpu_instancing_audit_samples" in audit
for field in ("gpu_instancing_audit_samples",
              "gpu_instancing_source_draws",
              "gpu_instancing_geometry_duplicates",
              "gpu_instancing_geometry_groups",
              "gpu_instancing_conservative_draws",
              "gpu_instancing_compatible_duplicates",
              "gpu_instancing_compatible_groups"):
    assert f"{field}=0;" in profile
    assert f"{field}+=o.{field};" in profile
assert "identity=cache_snapshot_id+geometry+group+MAT;not_filename" in renderer
assert "sampled_profile_only_no_instancing" in renderer
assert "instancing_rejections_all" in renderer
assert "instancing_rejections_repeated" in renderer
assert "record.rejection_mask=" in audit
assert "gpu_3do_instancing_rejection_mask(" in audit
assert "reason(specular_view_dependent" in audit
assert "gpu_3do_instancing_rejection_names" in header
assert "state_split_extras" in header
assert "result.repeated_member_ranges" in header
assert "result.rejects_repeated[i]" in header
assert "result.state_split_extras=" in header
assert "schedule_gpu_3do_instancing(" in header
assert "next.model_batch_group=item.batch_id;" in audit
assert "model_batch_count>1" in audit
assert "gpu_3do_instanced_calls" in audit
assert "gpu_3do_instanced_ranges" in profile
assert "enable_gpu_3do_instancing" in read("common/config.hpp")
assert 'j.contains("enable_gpu_3do_instancing")' in read("common/config.cpp")
assert "enable_gpu_3do_instancing" in read("doc/config.md")
for field in ("gpu_instancing_repeated_member_ranges",
              "gpu_instancing_repeated_eligible_ranges",
              "gpu_instancing_rejected_ranges",
              "gpu_instancing_repeated_rejected_ranges",
              "gpu_instancing_eligible_repeat_extras",
              "gpu_instancing_state_split_extras"):
    assert f"{field}=0;" in profile
    assert f"o.{field};" in profile
for field in ("gpu_instancing_rejects_all",
              "gpu_instancing_rejects_repeated"):
    assert f"{field}{{}};" in profile
    assert f"o.{field}[i];" in profile
assert "GPU 3DO CloneModel-safe instancing audit" in workflow
assert "tests\\gpu_3do_instancing_audit.cpp" in workflow
assert "tests/gpu_3do_instancing_audit.py" in workflow
assert '<ClInclude Include="gpu_3do_instancing_audit.hpp" />' in read(
    "renderer/renderer.vcxproj")
assert '<ClInclude Include="gpu_3do_instancing_audit.hpp">' in read(
    "renderer/renderer.vcxproj.filters")
print("3DO instancing feasibility audit and CloneModel identity checks passed")
