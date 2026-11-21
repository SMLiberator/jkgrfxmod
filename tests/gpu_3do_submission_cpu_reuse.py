"""Pin the main 3DO CPU submission fast path to source-level parity.

A cached GPU mesh is still uploaded on first ACTUAL draw, not earlier during
collection; transparent ordering and native model traversal are unchanged.
"""
from pathlib import Path

ROOT=Path(__file__).resolve().parents[1]
renderer=(ROOT/"renderer/renderer.cpp").read_text(encoding="utf-8")
main=(ROOT/"renderer/main.cpp").read_text(encoding="utf-8")
draw=renderer.split("void draw_gpu_3do_preview(",1)[1].split(
    "// Deliberate world-cache isolation test.",1)[0]
model=main.split("int __cdecl HookedNativeModel(",1)[1].split(
    "void __cdecl HookedNativeMesh(",1)[0]

# Each candidate's shared context is collected once per native Thing node
# and stays alive across the intervening decal and transparent passes.
assert "struct submission_context {" in draw
assert "gpu_3do_matrix34 modelview{};" in draw
assert "std::array<float,3> local_eye{};" in draw
assert "bool has_local_eye=false;" in draw
assert "bool reversed_winding=false;" in draw
assert "gpu_3do_gl_mesh *mesh=nullptr;" in draw
assert "submission_context *context;" in draw
assert "std::vector<submission_context> ordinary_contexts;" in draw
assert "std::vector<submission_context> isolated_contexts;" in draw
assert "auto &contexts=ordinary_pass" in draw
assert "if(!reuse_alpha) {" in draw
assert "contexts.reserve(instances.size());" in draw
assert "contexts.emplace_back()" in draw
assert "ordinary_alpha_serial=gpu_3do_present_serial;" in draw
assert "draws.swap(ordinary_alpha_draws);" in draw
assert "pending_draw candidate{&inst,&group,&tex,policy," in draw
assert "&context,depth," in draw

# Frustum/backface selection, lighting and material classification still use
# the same modelview/eye; they are merely computed once per Thing.
assert "native_frustum,context.modelview," in draw
assert "gpu_3do_face_facing_camera(" in draw
assert "context.has_local_eye=gpu_3do_local_eye(" in draw
assert draw.count("gpu_3do_local_eye(")==1
assert draw.count("gpu_3do_reversed_winding(inst.transform)")==1
assert "context.reversed_winding};" in draw
assert "next.context->reversed_winding!=reversed" in draw
assert "prog.set_local_eye(context.has_local_eye" in draw
assert "draws[n].context->modelview;" in draw
assert "context.reversed_winding" in draw

# No early GPU upload of culled or non-drawn geometry; the first actual draw
# owns the lookup and liveness timestamp, subsequent groups reuse its mesh.
assert "if(!context.mesh) {" in draw
assert "context.mesh=it->second.get();" in draw
assert "auto &gpu_mesh=*context.mesh;" in draw
assert draw.count("gpu_3do_last_drawn[inst.id]=gpu_3do_present_serial;")==1
assert draw.index("auto &context=*draw.context;") > draw.index("auto submit_draws=[&]")
assert draw.index("auto it=gpu_3do_gl_cache.find(inst.id);") > draw.index(
    "auto submit_draws=[&]")
assert draw.index("gpu_3do_last_drawn[inst.id]=gpu_3do_present_serial;") < (
    draw.index("if(active_instance!=draw.instance)")
)

# Preserve ordering and per-Thing GL uniforms for alpha, HUD and decals.
assert "std::sort(draws.begin(),draws.end()," in draw
assert "return a.depth>b.depth;" in draw
assert "gpu_3do_trail_alpha_mode(" in draw
assert "glMultiDrawElements(GL_TRIANGLES" in draw
assert "prog.set_lighting(inst," in draw
assert "prog.set_sector_crossing(inst.crossing_lighting.get()," in draw

# Native model-name parsing is debug-only and remains nested-call safe.
assert "char const *active_native_debug_model_filename()" in main
assert main.count("active_native_debug_model_filename()")>=3
assert "thread_local bool active_native_model_filename_ready=false;" in main
assert "bool const previous_model_filename_ready=" in model
assert "active_native_model_filename_ready=false;" in model
assert "active_native_model_filename_ready=previous_model_filename_ready;" in model
assert "active_native_model_filename=previous_model_filename;" in model
assert "std::array<char,33> native_model_filename" not in model
assert "RealNativeModel(thing,placement)" in model
assert "active_native_model_ptr=native_model_ptr;" in model

print("GPU 3DO submission context, draw order, deferred uploads and lazy diagnostics OK")
