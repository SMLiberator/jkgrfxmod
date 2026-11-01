"""Guard the GPU-only HDR meter/adaptation path after deleting its debug readback.

Source-level invariants; an actual GPU visual comparison remains necessary
for reports of altered exposure due to changed scene luminance.
"""
from pathlib import Path

root = Path(__file__).resolve().parents[1]
read = lambda path: (root / path).read_text(encoding="utf-8")

hdr = read("renderer/auto_exposure.hpp")
shader = read("assets/shaders/post_auto_exposure.frag")
presentation = read("assets/shaders/post_to_srgb.frag")
state_h = read("renderer/opengl_state.hpp")
state_cpp = read("renderer/opengl_state.cpp")
config_h = read("common/config.hpp")
config_cpp = read("common/config.cpp")
json_config = read("assets/jkgm.json")
readme = read("README.md")

# The retired option must not be silently kept as a render-thread readback.
for source in (hdr,shader,state_h,state_cpp,config_h,config_cpp,json_config):
    assert "profile_auto_exposure" not in source
    assert "auto_exposure_diagnostics" not in source
    assert "auto_exposure_last_log" not in source
assert "glReadPixels" not in hdr
assert "glReadBuffer" not in hdr
assert "glReadPixels(" not in shader
assert "retired `profile_auto_exposure`" in readme

# Keep the only live output (R), 1x1 previous-frame feedback, and scene input.
assert "out_color = vec4(adapted,0.0,0.0,1.0);" in shader
assert "float previous = target_ev;" in shader
assert "texelFetch(previous_exposure,ivec2(0),0).r" in shader
assert "texelFetch(exposure_image,ivec2(0),0).r" in presentation
assert "float adapted = mix(previous,target_ev,weight);" in shader
assert "ogs->auto_exposure_history_valid=true;" in hdr
assert "ogs->auto_exposure_next_a=!ogs->auto_exposure_next_a;" in hdr

# The algorithm, region and response times must not be disturbed by cleanup.
for expr in (
    "const int grid = 12;",
    "0.1 + 0.8*(vec2(float(x),float(y))+0.5)/float(grid)",
    "float highlight_average = min(rms_average,3.0*linear_average+0.05);",
    "float average_meter = mix(log_average,linear_average,0.4);",
    "float highlight_meter = mix(linear_average,highlight_average,0.85);",
    "float meter_lum = max(average_meter,highlight_meter);",
    "float rate = target_ev<previous ? 3.0 : 1.2;",
    "float weight = 1.0-exp(-rate*clamp(exposure_params.y,0.0,0.25));",
):
    assert expr in shader, expr
assert "ogs->auto_exposure_sensitivity>0.0f" in hdr
assert "ogs->enable_hdr;" in hdr
for compositor in ("renderer/renderer_compositor_basic.cpp",
                   "renderer/renderer_compositor_bloom.cpp"):
    assert "update_auto_exposure(ogs,composed_screen);" in read(compositor)
print("HDR exposure algorithm invariants and diagnostic cleanup passed")
