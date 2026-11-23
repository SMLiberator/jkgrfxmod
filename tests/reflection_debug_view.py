"""Capture-projection debug view: default-off, capture activation, world-ray sampling."""
from pathlib import Path
import json
root=Path(__file__).resolve().parents[1]
cfg=(root/"common/config.hpp").read_text()
parser=(root/"common/config.cpp").read_text()
sample=json.loads((root/"assets/jkgm.json").read_text())
renderer=(root/"renderer/renderer.cpp").read_text()
capture=(root/"renderer/hemi_reflection_capture.inc").read_text()
shader=(root/"assets/shaders/reflection_debug_view.frag").read_text()
header=(root/"renderer/reflection_debug_view_shader.hpp").read_text()
assert 'debug_reflection_mode = "off"' in cfg
assert sample["debug_reflection_mode"]=="off"
for mode in ("off","cube","hemi","shared"):
    assert f'vr->debug_reflection_mode!="{mode}"' in parser or mode=="off"
assert "debug_reflection_owner_key" in renderer
assert "draw_reflection_debug_view();" in renderer
assert "frame_world_cache_camera.camera" in renderer
assert "get<x>(conf_scr_res),get<y>(conf_scr_res)" in renderer
assert "it->second.target->color_texture()" in renderer
assert "debug.is_debug=true;" in capture
assert "debug.position=viewer.position;" in capture
assert 'debug_reflection_mode=="shared"' in capture
assert "candidate.is_debug?0.0f:" in capture
assert "shader.set_projection(hemi)" in renderer
assert "GL_TEXTURE_2D_ARRAY" in renderer
assert "postprocess.vert" in header
assert "tan_half_fov*height_over_width" in shader
assert "smoothstep(-0.087155743,0.087155743,d.z)" in shader
assert "1.091308501" in shader
for fragment in ("layer=d.x>=0.0?0:1","layer=d.y>=0.0?2:3","layer=d.z>=0.0?4:5"):
    assert fragment in shader
assert "color.rgb/color.a" in shader
print("Reflection debug view wiring passed")
