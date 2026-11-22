"""Compact HDR G-buffer A/B switch, format, and pigment/light ABI contracts."""
from pathlib import Path
import json
root=Path(__file__).resolve().parents[1]
read=lambda path:(root/path).read_text(encoding="utf-8")
sample=json.loads(read("assets/jkgm.json"))
cfg=read("common/config.hpp")
parser=read("common/config.cpp")
docs=read("doc/config.md")
basic=read("renderer/renderer_screen_basic.cpp")
msaa=read("renderer/renderer_screen_msaa.cpp")
screen=read("renderer/renderer_screen.cpp")
hdr=read("glutil/framebuffer.hpp")
fmt=read("glutil/texture.hpp")
shader_cpp=read("renderer/shaders.cpp")
shader_h=read("renderer/shaders.hpp")
renderer=read("renderer/renderer.cpp")
compose=read("assets/shaders/post_opaque_composite.frag")
helper=read("assets/shaders/gbuffer_normal_packed.glsl")
units=read("renderer/texture_units.hpp")

assert sample["enable_gbuffer_packing"] is False
assert "enable_gbuffer_color_packing" not in sample
assert "bool enable_gbuffer_packing = false;" in cfg
assert "bool enable_gbuffer_color_packing" not in cfg
assert 'j.contains("enable_gbuffer_packing")' in parser
assert 'j.contains("enable_gbuffer_color_packing")' in parser  # migration alias
assert "rv->enable_gbuffer_packing" in parser
assert "| **enable_gbuffer_packing** |" in docs
assert "| **enable_gbuffer_color_packing** |" not in docs
assert "r11f_g11f_b10f = 0x8C3A" in fmt
assert "GBUFFER opaque_color=" in screen
assert screen.count("the_config->enable_gbuffer_packing") == 6
assert "configure_gbuffer_packing(" in shader_cpp
assert "configure_gbuffer_packing(" in shader_h
assert "gbuffer_packing_uniform" in shader_cpp
assert "gbuffer_packing_enabled?1:0" in shader_cpp
assert "opengl_shader::configure_gbuffer_packing(" in renderer
assert "uniform int gbuffer_packing;" in helper

# RGB11F color and emission are both 4-byte HDR. Legacy RGB(A) remain 16F.
assert basic.count("packed_color ? gl::texture_internal_format::r11f_g11f_b10f")==2
assert msaa.count("packed_color ? gl::texture_internal_format::r11f_g11f_b10f")==4
assert "gl::texture_internal_format::rgba16f" in basic and "gl::texture_internal_format::rgba16f" in msaa
assert "gl::texture_internal_format::rgb10_a2" in basic and "gl::texture_internal_format::rgb10_a2" in msaa
assert "gl::texture_internal_format::r16f" in basic and "gl::texture_internal_format::r16f" in msaa
assert "gbuffer_emissive_resolve(screen_res," in msaa
assert "gbuffer_normal_resolve(screen_res," in msaa
assert "color4 = 0x8CE4" in hdr

# Pigment has independent 4-byte HDR storage only if deferred lighting is
# enabled; otherwise no fifth attachment or MSAA resolve exists.
for s in (basic,msaa):
    assert "has_pigment=packed_color && spotlight_albedo;" in s
    assert "gl::framebuffer_attachment::color4" in s
    assert "gl::draw_buffer::color4" in s
    assert "if(screen_gbuffer.has_pigment)gl::clear_buffer_color(4" in s
assert "std::make_unique<msaa_resolve_buffer>" in msaa
assert "if(gbuffer_pigment_resolve)" in msaa
assert "gl::read_buffer(gl::framebuffer_attachment::color4)" in msaa
assert "get_resolved_pigment_texture()" in renderer
assert "pigment_image" in compose and '"pigment_image",4,sampler_kind::tex2d' in units
assert "gbuffer_packing!=0" in compose
assert "texelFetch(pigment_image,tc,0).rgb" in compose
assert "texelFetch(depth_image,tc,0).gba" in compose
assert "the_config->enable_spotlights || the_config->enable_sun" in screen

# No second deferred-lighting calculation: color and emissive are still
# composed separately so SSAO/sun/fog cannot affect emissive contribution.
assert "vec3 surface_rgb=albedo.rgb*occlusion;" in compose
assert "out_color=vec4(emissive.rgb+lit,covered?albedo.a:0.0);" in compose
for p in ("game_opaque_pass","world_cache","gpu_3do_preview"):
    source=read(f"assets/shaders/{p}.frag")
    assert "out_pigment" in source and "out_depth" in source
    assert "gbuffer_pack_normal(" in source

# Projected decals sample a single source in either mode: legacy emissive.A
# or packed normal.B. The light scalar survives bounded [0,1.25] packing.
decal=read("assets/shaders/gpu_3do_preview.frag")
assert "gbuffer_receiver_light(texelFetch(decal_light_texture,pixel,0))" in decal
assert "texelFetch(decal_light_texture,pixel,0).a" in decal
assert "frame_screen->get_resolved_emissive_texture()" in renderer
assert "frame_screen->get_resolved_normal_texture()" in renderer
assert "gbuffer_is_sprite" in compose
assert "gbuffer_is_sprite" in read("assets/shaders/post_sun_visibility_half.frag")
print("Full G-buffer packing: both A/B layouts, emissive/decal channels, HDR pigment and MSAA resolve OK")
