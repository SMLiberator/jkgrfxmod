"""Shared GL state filter: no stale capture bindings or direct draw-loop calls."""
from pathlib import Path
root=Path(__file__).resolve().parents[1]
read=lambda p:(root/p).read_text(encoding="utf-8")
gl=read("glutil/gl.cpp")
hdr=read("glutil/gl.hpp")
textures=read("glutil/texture.cpp")
vao=read("glutil/vertex_array.cpp")
renderer=read("renderer/renderer.cpp")
target=read("renderer/offscreen_capture_target.hpp")
for call in ("invalidate_cached_state", "set_active_texture_unit_raw",
             "bind_texture_raw", "bind_vertex_array_raw",
             "set_front_face_raw", "set_cull_face_enabled",
             "consume_state_cache_stats"):
    assert call in hdr and call in gl,call
assert "thread_local cached_submission_state submission_state;" in gl
assert "cache.active_unit==unit" in gl
assert "cache.vao==vao" in gl
assert "*slot==texture" in gl
assert "cache.cull_enabled==int(enabled)" in gl
assert "bind_texture_raw(static_cast<GLenum>(target), *tex);" in textures
assert "set_active_texture_unit_raw(unit);" in textures
assert "bind_vertex_array_raw(*vao);" in vao
assert "invalidate_cached_state();" in textures
assert "invalidate_cached_state();" in vao
assert "gl::invalidate_cached_state();" in renderer
assert "PERF GL state calls active=" in renderer
assert target.count("offscreen_capture_state_invalidator invalidate_on_return;")>=6
for name in ("renderer/renderer.cpp",
             "renderer/hemi_reflection_capture.inc",
             "renderer/reflection_sky_capture.inc",
             "renderer/camera_to_mat_capture.inc",
             "renderer/sun_caustics_gpu.hpp",
             "renderer/sun_sky_aperture_gpu.hpp"):
    source=read(name)
    for direct in ("glActiveTexture(", "glBindTexture(",
                   "glBindVertexArray(", "glFrontFace(", "glCullFace(",
                   "glEnable(GL_CULL_FACE)", "glDisable(GL_CULL_FACE)"):
        assert direct not in source,(name,direct)
print("Shared main/reflective GL state cache and capture invalidation OK")
