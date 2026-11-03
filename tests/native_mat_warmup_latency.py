"""GPU MAT warmup discovers all missing 3DO cels in one safe deferred batch."""
from pathlib import Path
root=Path(__file__).resolve().parents[1]
main=(root/"renderer/main.cpp").read_text(encoding="utf-8")
queue=(root/"renderer/native_mat_warm_queue.hpp").read_text(encoding="utf-8")
renderer=(root/"renderer/renderer.cpp").read_text(encoding="utf-8")
start=main.index("bool gpu_mesh_materials_ready(")
end=main.index("uint64_t elapsed_native_ns(",start)
body=main[start:end]
assert "bool missing_native_texture=false;" in body
assert "missing_native_texture=true;" in body
assert "return !missing_native_texture;" in body
assert 'return fail("unloaded_native_texture"' not in body
assert "get_native_mat_warm_queue().request(" in body
assert 'fail("unloaded_native_texture",ref.first,cel,&mat,&tex)' in body
assert "gpu_mat_residency.mark_ready(" in body
# Verify unchanged safe call order; no uploads from a draw callback.
tick=main.index("void __cdecl HookedNativeThingUpdate(")
service_call=main.index("            service_native_mat_warm_queue();",tick)
native_update=main.index("RealNativeThingUpdate(seconds,milliseconds);",tick)
assert native_update<service_call
service=main[main.index("void service_native_mat_warm_queue()\n"):]
assert "max_uploads_per_tick=12U" in service
assert "std::chrono::milliseconds(2)" in service
assert "queue.release_unattempted(request);" in service
assert "queue.finish(request,warm_gpu_mat_cel(" in service
assert "native_default_mat_uploader_ready(tex.header[0])" in main
assert "jkgm::native_mat_default_upload_variant" in main
assert "entry.second=tick_+30" in queue
assert "if(it!=pending_.end())it->second=tick_;" in queue
assert "get_native_mat_warm_queue().prepare_world(" in renderer
print("MAT batch discovery and safe native tick throttle verified")
