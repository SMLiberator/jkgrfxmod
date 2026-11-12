"""Source/packaging contracts for the experimental planar capture pass."""
import json
from pathlib import Path
root=Path(__file__).resolve().parents[1]
read=lambda path:(root/path).read_text(encoding="utf-8")
settings=json.loads(read("assets/jkgm.json"))
assert settings["experimental_planar_reflection_capture"] is False
assert settings["planar_reflection_resolution_scale"]==0.5
assert settings["planar_reflection_debug_view"] is False
config=read("common/config.cpp")
for name in ("experimental_planar_reflection_capture",
             "planar_reflection_resolution_scale","planar_reflection_debug_view"):
    assert f'j.contains("{name}")' in config
assert 'std::isfinite(v)' in config and 'v<0.125f || v>1.0f' in config
renderer=read("renderer/renderer.cpp")
target=read("renderer/planar_reflection_capture_target.hpp")
shader=read("renderer/planar_reflection_capture_shader.hpp")
v=read("assets/shaders/planar_capture.vert")
f=read("assets/shaders/planar_capture.frag")
assert "render_planar_capture();" in renderer
assert "select_planar_capture_plane(" in renderer
# Passable adjoins frequently carry water: eligible as reflective RECEIVERS,
# while the mirrored scene itself must still omit passable portal geometry.
selector=renderer.split("void select_planar_capture_plane(",1)[1].split(
    "void render_planar_capture()",1)[0]
assert "surface.count<3U || surface.passable_adjoin" not in selector
assert "if(surface.passable_adjoin)++matched_passable;" in selector
assert "mat.blend==material_blend_mode::fluid ? 1 : 0" in selector
assert "passable_reflection_receivers=" in selector
capture=renderer.split("void render_planar_capture()",1)[1]
assert "surface.count==0U || surface.passable_adjoin" in capture
assert "make_planar_reflection_camera(" in renderer
assert "make_planar_reflection_render_setup(" in renderer
assert "planar_reflection_gl_scope restore;" in renderer
assert "for(auto const &surface:cache.mesh.surfaces)" in renderer
assert "collect_planar_reflection_things(read,world,scan)" in renderer
assert "acquire_planar_reflection_offscreen_gpu(" in renderer
assert "gpu_3do_native_modelview(" in renderer
assert "planar_capture_target->framebuffer()" in renderer
assert "glBlitFramebuffer(" in renderer
assert "GL_COLOR_ATTACHMENT0" in target
assert "GL_DEPTH_COMPONENT24" in target
assert "glGetIntegerv(GL_DRAW_FRAMEBUFFER_BINDING" in target
assert "glBindFramebuffer(GL_DRAW_FRAMEBUFFER" in target
assert "clip_plane_view" in v and "if(clip_side<" in f
assert "is_model" in v and "native_uv_offset" in v
assert "albedo_map" in f and "alpha_cutoff" in f
for p in ("renderer/renderer.vcxproj","renderer/renderer.vcxproj.filters"):
    s=read(p)
    for name in ("planar_reflection_capture_target.hpp",
                 "planar_reflection_capture_shader.hpp",
                 "planar_reflection_render_math.hpp"):
        assert name in s
assert 'Copy-Item -Path \'assets/shaders/*\'' in read(".github/workflows/main.yml")
print("Configurable planar FBO/camera/mirror capture source contracts OK")
