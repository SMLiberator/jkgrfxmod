"""Keep pending 3DO draws on stable frame-cached material references.

This source contract protects the copy-elimination optimization in the
ordinary opaque/alpha and delayed Camera HUD passes.
"""
from pathlib import Path

renderer=(Path(__file__).resolve().parents[1]/"renderer/renderer.cpp").read_text(encoding="utf-8")
start=renderer.index("void draw_gpu_3do_preview(")
end=renderer.index("// Deliberate world-cache isolation test.", start)
draw=renderer[start:end]

# The pending list must not copy full material structures for every
# potentially one-triangle 3DO range.
assert "gpu_3do_texture const *texture;" in draw
assert "gpu_3do_texture texture;" not in draw
assert "pending_draw candidate{&inst,&group,&tex,policy," in draw
assert "auto const &material=*draw.texture;" in draw
assert draw.count("draw.texture")==1
assert "auto const &tex=found->second;" in draw

# Separate lifetime boundaries: native MATs are resolved in the current
# frame, while the delayed Camera HUD uses a retained material snapshot.
assert "std::unordered_map<uint64_t,gpu_3do_texture> gpu_frame_material_cache;" in renderer
assert "std::unordered_map<uint64_t,gpu_3do_texture> deferred_gpu_hud_material_cache;" in renderer
assert "auto &mat_cache=post_exempt" in draw
assert "? deferred_gpu_hud_material_cache" in draw
assert ": gpu_frame_material_cache;" in draw
assert "if(post_exempt) continue;" in draw
assert "deferred_gpu_hud_material_cache.emplace(" in renderer
assert "retain_deferred_gpu_hud_material(found->second);" in renderer

# The collect/sort and the submit loop remain within this call, so none of
# their borrowed pointers can outlive the corresponding material cache.
assert draw.index("pending_draw candidate{") < draw.index("std::sort(draws.begin()")
assert draw.index("std::sort(draws.begin()") < draw.index("auto const &material=*draw.texture;")
print("GPU 3DO pending material borrowing checks passed")
