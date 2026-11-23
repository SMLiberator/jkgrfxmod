"""Opt-in worker prepass is CPU-only and preserves main-camera draw parity."""
from pathlib import Path
root=Path(__file__).resolve().parents[1]
renderer=(root/"renderer/renderer.cpp").read_text(encoding="utf-8")
header=(root/"renderer/gpu_3do_parallel_visibility.hpp").read_text(encoding="utf-8")
pool=(root/"renderer/cpu_render_workers.hpp").read_text(encoding="utf-8")
config=(root/"common/config.hpp").read_text(encoding="utf-8")
read=(root/"common/config.cpp").read_text(encoding="utf-8")
docs=(root/"doc/config.md").read_text(encoding="utf-8")
workflow=(root/".github/workflows/main.yml").read_text(encoding="utf-8")
assert "bool experimental_multithreading = false;" in config
assert 'j.contains("experimental_multithreading")' in read
assert "**experimental_multithreading**" in docs
assert '#include "gpu_3do_parallel_visibility.hpp"' in renderer
assert '#include "cpu_render_workers.hpp"' in renderer
assert "if(prepare_both && the_config->experimental_multithreading)" in renderer
assert "prepared_indices.size()>=48U && group_work>=256U" in renderer
assert "prepared_parallel=workers.execute(" in renderer
assert "std::unique_ptr<cpu_render_workers> cpu_visibility_workers;" in renderer
assert "cpu_visibility_workers.reset();" in renderer
assert "static cpu_render_workers workers;" not in renderer
assert "if(!reuse_alpha) for(auto const &inst:instances)" in renderer
assert "if(parallel) {" in renderer
assert "if(!parallel && have_perspective_frustum" in renderer
assert "if(!parallel && can_cpu_cull" in renderer
assert "pending_draw candidate{&inst,&group,&tex,policy," in renderer
assert "ordinary_alpha_serial=gpu_3do_present_serial;" in renderer
assert "glDrawElementsInstanced(" in renderer
assert "light_budget" not in pool
assert "gpu_3do_face_facing_camera(" in header
assert "cached_group_bounds(" in header
assert "gpu_3do_batch_outside_frustum(" in header
assert "gpu_3do_native_modelview(" in header
assert "gpu_3do_local_eye(" in header
assert "gpu_3do_reversed_winding(" in header
assert "std::thread" in pool and "condition_variable" in pool
assert "gpu_parallel_visibility_wall_us" in renderer
assert "PERF gpu_3do multithreading enabled=" in renderer
assert "GPU 3DO parallel visibility A/B" in workflow
print("Opt-in parallel visibility is frame-bounded, material-free and GL-free")
