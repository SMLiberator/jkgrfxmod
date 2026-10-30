"""Regression checks for the COG-controlled RGB split final post-FX pass.

No GL context needed: verify shader wiring and pixel-space radial geometry.
"""
from math import hypot, isclose
from pathlib import Path

root = Path(__file__).resolve().parent.parent
shader = (root / "assets/shaders/post_to_srgb.frag").read_text(encoding="utf-8")
postfx = (root / "renderer/postfx.hpp").read_text(encoding="utf-8")
cog = (root / "renderer/main.cpp").read_text(encoding="utf-8")
shaders_h = (root / "renderer/shaders.hpp").read_text(encoding="utf-8")
shaders_cpp = (root / "renderer/shaders.cpp").read_text(encoding="utf-8")

# No new compositor is needed: both existing compositors call apply_post_fx.
for path in ("renderer/renderer_compositor_basic.cpp",
             "renderer/renderer_compositor_bloom.cpp"):
    assert "apply_post_fx(ogs->post_to_srgb, ogs->enable_post);" in (
        root / path).read_text(encoding="utf-8")

assert "uniform float rgb_split_pixels = 0.0;" in shader
assert "if(rgb_split_pixels > 0.0)" in shader
assert "vec4 samp = texelFetch(fbuf_image, sc, 0);" in shader
assert "samp.r=texture(fbuf_image," in shader
assert "samp.b=texture(fbuf_image," in shader
assert "samp.g=" not in shader
assert "samp.a=" not in shader
assert "center_uv+shift_uv" in shader
assert "center_uv-shift_uv" in shader
assert "vec2 half_dims=0.5*dims;" in shader
assert "max(length(half_dims),1.0)" in shader
assert "min(rgb_split_pixels,32.0)" in shader
assert "vec4 bloom_samp = vec4(0.0);" in shader

assert "animated_floats<1> pixels{std::array<float, 1>{0.0f}};" in postfx
assert "shader.set_rgb_split(0.0f);" in postfx  # disable post
assert "shader.set_rgb_split(std::isfinite(split)" in postfx
assert "std::clamp(split,0.0f,32.0f)" in postfx
assert "gl::set_uniform_float(rgb_split_pixels,pixels);" in shaders_cpp
assert 'rgb_split_pixels(gl::get_uniform_location(the_program, "rgb_split_pixels"))' in shaders_cpp
assert "void set_rgb_split(float pixels);" in shaders_h

for verb in ("setrgbsplit", "getrgbsplit", "resetrgbsplit"):
    assert 'cog_verb_entry{"'+verb+'",' in cog
assert "get_rgb_split_settings().pixels.fade_to(" in cog
assert "get_rgb_split_settings()=jkgm::rgb_split_settings();" in cog

# Reference implementation of the shader's pixel-space offset. The
# diagonally opposite screen corners have equal radial distance, and the
# center must not move. On wide screens pixel-space radial math must not
# distort horizontal/vertical displacement lengths.
def offset_px(x, y, width, height, strength):
    half_w, half_h = width/2, height/2
    normalizer = max(hypot(half_w, half_h), 1.0)
    return ((x-half_w)*strength/normalizer,
            (y-half_h)*strength/normalizer)

for w,h in ((640,480),(1920,1080),(2560,1080)):
    assert offset_px(w/2,h/2,w,h,8) == (0.0,0.0)
    for corner in ((0,0),(w,0),(0,h),(w,h)):
        dx,dy=offset_px(*corner,w,h,8)
        assert isclose(hypot(dx,dy),8.0,abs_tol=1e-6)
    # The shader clamps the source UV to 0..1 so even strong splits
    # never sample out-of-bounds data or wrap to the opposite edge.
    dx,dy=offset_px(w,h,w,h,32)
    assert dx>0 and dy>0
print("COG RGB split post-FX regression checks passed")
