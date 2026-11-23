"""Reflection worker jobs preserve scene/GL boundaries and source ordering."""
from pathlib import Path
root=Path(__file__).resolve().parents[1]
read=lambda path:(root/path).read_text(encoding="utf-8")
r=read("renderer/renderer.cpp")
h=read("renderer/hemi_reflection_capture.inc")
helper=read("renderer/reflection_capture_parallel_visibility.hpp")
config=read("common/config.hpp")
assert "experimental_multithreading = false;" in config
assert '#include "reflection_capture_parallel_visibility.hpp"' in r
assert "capture_parallel=workers.execute(" in h
assert "planar_parallel=workers.execute(" in r
assert "if(the_config->experimental_multithreading)" in h
assert "if(the_config->experimental_multithreading)" in r
assert "capture_parallel_work=size_t(face_count)*" in h
assert "if(capture_parallel_work>=512U)" in h
assert "planar_parallel_work=group_work+planar_frame_candidates.size();" in r
assert "if(planar_parallel_work>=512U)" in r
assert "for(size_t i=0;i<capture_models.size();++i)" in h
assert "for(size_t i=0;i<planar_models.size();++i)" in r
assert "capture_world_visibility" in h and "planar_world_rejected" in r
assert "capture_model_visibility" in h and "planar_model_visibility" in r
assert "reflection_capture_cull::portal" in h
assert "paraboloid_mode,0.0f" in h # no world-distance cutoff
assert "false,0.0f,planar_model_visibility[i]" in r
assert "reflection_probe_bounds_outside_range" in helper
assert "dual_paraboloid_bounds_outside" in helper
assert "gpu_3do_batch_outside_frustum" in helper
assert "out.ready=false;" in helper
assert "geo.cached_group_bounds" in helper
for text,needle in ((h,"capture_parallel=workers.execute("),
                    (r,"planar_parallel=workers.execute(")):
    job=text.split(needle,1)[1].split("\n                                });",1)[0] if text is h else text.split(needle,1)[1].split("\n                        });",1)[0]
    assert "gl::" not in job and "glDraw" not in job
    assert "engine_material_debug" not in job
    assert "resolve_gpu_3do_material" not in job
assert "if(!parallel && inst.geometry->cached_group_bounds" in h
assert "!parallel &&" in r
print("Reflection capture parallel visibility contracts OK")
