"""Planar material mode and independent camera groundwork (no capture yet)."""
from pathlib import Path
root=Path(__file__).resolve().parents[1]
read=lambda p:(root/p).read_text(encoding="utf-8")
enums=read("common/material.hpp")
parser=read("common/material_map.cpp")
assert "enum class material_reflection_mode { envmap, screen, fixed, matcap, planar };" in enums
assert 'else if(v == "planar")' in parser
assert "material_reflection_mode::planar" in parser
for path in ("assets/shaders/game_surface_common.glsl",
             "assets/shaders/world_cache.frag",
             "assets/shaders/gpu_3do_preview.frag"):
    assert "mode == 4" in read(path) or "mode==4" in read(path),path
header=read("renderer/planar_reflection_camera.hpp")
assert "make_planar_reflection_camera(" in header
assert "planar_camera_native_view(" in header
assert "gpu_3do_matrix34" in header
assert "read_active_render_camera()" not in header
assert "gpu_3do_native_modelview" in read("tests/planar_reflection_camera.cpp")
for proj in ("renderer/renderer.vcxproj","renderer/renderer.vcxproj.filters"):
    assert 'planar_reflection_camera.hpp' in read(proj)
assert "Planar" in read("doc/planar_reflections.md")
print("planar material and independent mirrored-camera groundwork OK")
