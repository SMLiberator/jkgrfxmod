"""Source-level regression checks for late-pass Camera Thing emissive MATs.

The GPU implementation lives in GLSL, so this guards against accidentally
moving emissive evaluation after the post-exempt early return again.
"""
from pathlib import Path

shader = (Path(__file__).resolve().parent.parent
          / "assets/shaders/gpu_3do_preview.frag").read_text(encoding="utf-8")
main = shader.split("void main() {", 1)[1]
hud = main.split("    if(camera_hud_thing!=0) {", 1)[1].split(
    "        return;", 1)[0]
pre_hud = main.split("    if(camera_hud_thing!=0) {", 1)[0]
post_hud = main.split("        return;", 1)[1]

# Late pass must retain zero-alpha emissive-only fragments.
assert "if(albedo.a<=0.001 && emissive_only==0) discard;" in pre_hud
# Use the same emissive strength, texture and UV scrolling as the world.
assert pre_hud.count("vec3 emission=emissive_factor;") == 1
assert "emission*=texture(emissive_map,uv+emissive_scroll).rgb;" in pre_hud
assert "vec3 emission=emissive_factor;" not in post_hud

# Ordinary and additive HUD pigment are premultiplied, but emission is
# independent of albedo alpha; do not make it alpha * emission.
assert "out_color=vec4(albedo.rgb*a+emission,a);" in hud
assert "out_color=vec4(albedo.rgb*a+emission,0.0);" in hud
# Multiply retains its old destination-color semantics (alpha zero is
# the white/identity multiplier), not independent additive emission.
assert "clamp(albedo.rgb+emission,0.0,1.0),a)" in hud
assert "out_emissive=vec4(0.0);" in hud

# Mathematical consequences of the shader's normal premultiplied path.
def over(dst, pigment, alpha, emission):
    src = [pigment[i]*alpha+emission[i] for i in range(3)]
    return [src[i]+dst[i]*(1-alpha) for i in range(3)]

def close(a, b):
    return abs(a-b)<1.e-6

# Fully transparent albedo still adds emission on top of existing HUD.
dst = [0.2, 0.3, 0.4]
out = over(dst, [1.0, 1.0, 1.0], 0.0, [0.5, 0.0, 0.1])
assert all(close(a,b) for a,b in zip(out, [0.7, 0.3, 0.5]))
# Ordinary translucent pigment and independent emission can coexist.
out = over(dst, [0.8, 0.4, 0.0], 0.5, [0.1, 0.2, 0.0])
assert all(close(a,b) for a,b in zip(out, [0.6, 0.55, 0.2]))
# No emitter: normal alpha blending remains unchanged.
out = over(dst, [1.0, 1.0, 1.0], 0.0, [0.0, 0.0, 0.0])
assert all(close(a,b) for a,b in zip(out,dst))
print("Camera HUD emissive-only shader regression checks passed")
