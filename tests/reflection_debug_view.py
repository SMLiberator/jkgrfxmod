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
    assert f'rv->debug_reflection_mode!="{mode}"' in parser
assert "debug_reflection_owner_key" in renderer
assert "draw_reflection_debug_view();" in renderer
assert "reflection_debug_display_camera" in renderer
assert "reflection_debug_display_serial" in renderer
assert "reflection_debug_display_serial!=" in renderer
assert "gpu_3do_present_serial-1U" in renderer
assert "it->second.seen_frame!=reflection_debug_display_serial" in renderer
assert "reflection_debug_display_serial=gpu_3do_present_serial;" in capture
assert "reflection_debug_display_camera=viewer;" in capture
assert "get<x>(conf_scr_res),get<y>(conf_scr_res)" in renderer
assert "it->second.target->color_texture()" in renderer
assert "debug.is_debug=true;" in capture
assert "debug.position=viewer.position;" in capture
assert 'debug_reflection_mode=="shared"' in capture
assert "debug_reflection_distance_culling" not in cfg
assert "debug_reflection_distance_culling" not in sample
assert "debug_reflection_distance_culling" not in parser
assert "candidate.is_debug?0.0f:" not in capture
assert "float const interval=candidate.is_global?0.0f:" in capture
assert "bool const debug_supported=fragment_units>=2" in capture
assert "bool const debug_supported=realtime_supported" not in capture
assert "the_config->enable_reflection_capture being on" in capture
assert 'missing="capture_not_from_presented_frame"' in renderer
assert 'missing="probe_not_created"' in renderer
assert 'missing="probe_not_ready"' in renderer
assert 'missing="probe_frame_mismatch"' in renderer
assert 'REFLECTION debug view unavailable mode=' in renderer
assert "glClear(GL_COLOR_BUFFER_BIT);" in renderer
assert "reflection_probe_capture_distance_limit(" in capture
assert "thing_distance_limit=" in capture
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
