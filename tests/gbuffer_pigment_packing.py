"""HDR RGB pigment storage never replaces linear R16F depth when packed."""
from pathlib import Path
R=Path(__file__).resolve().parents[1]
read=lambda p:(R/p).read_text(encoding="utf-8")
b=read("renderer/renderer_screen_basic.cpp")
m=read("renderer/renderer_screen_msaa.cpp")
cpp=read("renderer/renderer.cpp")
composite=read("assets/shaders/post_opaque_composite.frag")
for s in (b,m):
    assert "(spotlight_albedo && !packed_color)" in s
    assert "has_pigment=packed_color && spotlight_albedo;" in s
    assert "gl::texture_internal_format::r11f_g11f_b10f" in s
    assert "gl::texture_internal_format::r16f" in s
    assert "gl::framebuffer_attachment::color4" in s
assert "gbuffer_pigment_resolve(packed_color && spotlight_albedo" in m
assert "gl::read_buffer(gl::framebuffer_attachment::color4)" in m
assert "frame_screen->get_resolved_depth_texture()" in cpp
assert "frame_screen->get_resolved_pigment_texture()" in cpp
assert "texelFetch(depth_image,tc,0).r" in composite
assert "texelFetch(pigment_image,tc,0).rgb" in composite
for writer in ("game_opaque_pass","world_cache","gpu_3do_preview"):
    glsl=read("assets/shaders/"+writer+".frag")
    assert "layout(location=4) out vec4 out_pigment;" in glsl or "layout(location = 4) out vec4 out_pigment;" in glsl
    assert "out_pigment" in glsl
print("Packed pigment: R16F scene depth retained, RGB11F unlit light receiver and conditional MSAA resolve OK")
