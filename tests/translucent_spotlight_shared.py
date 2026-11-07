"""The native and cached 3DO translucent spotlight receivers share one implementation.

GLSL #include is expanded at load time. Keep both paths byte-identical in
shared code rather than allowing shadow-bias and cookie drift.
"""
from pathlib import Path

root=Path(__file__).resolve().parents[1]
base=root/"assets"/"shaders"
common=(base/"translucent_spotlight_common.glsl").read_text(encoding="utf-8")
include='#include "translucent_spotlight_common.glsl"'
for filename in ("game_trns_pass.frag","gpu_3do_preview.frag"):
    raw=(base/filename).read_text(encoding="utf-8")
    assert raw.count(include)==1,filename
    assert "uniform int translucent_sun_enabled" in raw,filename
    # Calls remain in shader-specific composition; only the shared
    # function declarations/implementations are moved.
    for declaration in ("float translucent_shadow_visibility(",
                        "vec3 translucent_spotlight_world_pos(",
                        "vec3 translucent_spotlight_world_normal(",
                        "vec3 light_translucent_spotlight("):
        assert declaration not in raw, (filename,declaration)
        assert common.count(declaration)==1, (filename,declaration)
    expanded=raw.replace(include,common)
    assert "float translucent_shadow_visibility(" in expanded
    assert "vec4 sample_translucent_spotlight(" in expanded
    assert "vec3 translucent_spotlight_world_normal(" in expanded
    assert expanded.count("vec3 light_translucent_spotlight(")==1
    assert expanded.index("uniform int translucent_sun_enabled") > expanded.index(
        "vec3 translucent_spotlight_world_normal(")

for token in ("uniform float translucent_shadow_angle_bias_scale;",
              "0.015+0.035*translucent_shadow_angle_bias_scale*",
              "visible*0.25;",
              "translucent_spotlight_camera_forward.xyz"):
    assert token in common,token
print("Shared translucent spotlight GLSL include and parity OK")
