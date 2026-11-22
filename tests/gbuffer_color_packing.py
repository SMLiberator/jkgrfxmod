"""Opt-in opaque R11F_G11F_B10F color target wiring and MSAA/alpha parity."""
from pathlib import Path
import json
root=Path(__file__).resolve().parents[1]
read=lambda path:(root/path).read_text(encoding="utf-8")
config=read("common/config.hpp")
parser=read("common/config.cpp")
sample=json.loads(read("assets/jkgm.json"))
docs=read("doc/config.md")
fmt=read("glutil/texture.hpp")
screen=read("renderer/renderer_screen.cpp")
basic_h=read("renderer/renderer_screen_basic.hpp")
basic=read("renderer/renderer_screen_basic.cpp")
msaa_h=read("renderer/renderer_screen_msaa.hpp")
msaa=read("renderer/renderer_screen_msaa.cpp")
compose=read("assets/shaders/post_opaque_composite.frag")
assert "bool enable_gbuffer_color_packing = false;" in config
assert 'j.contains("enable_gbuffer_color_packing")' in parser
assert "rv->enable_gbuffer_color_packing" in parser
assert sample["enable_gbuffer_color_packing"] is False
assert "| **enable_gbuffer_color_packing** |" in docs
assert "r11f_g11f_b10f = 0x8C3A" in fmt
assert "GBUFFER opaque_color=" in screen
assert screen.count("the_config->enable_gbuffer_color_packing")==4
for path,s in (("basic.hpp",basic_h),("msaa.hpp",msaa_h),
               ("basic.cpp",basic),("msaa.cpp",msaa)):
    assert "bool packed_color" in s,path
assert basic.count("packed_color ? gl::texture_internal_format::r11f_g11f_b10f")==1
assert msaa.count("packed_color ? gl::texture_internal_format::r11f_g11f_b10f")==2
assert "gbuffer_color_resolve(screen_res," in msaa
assert "gl::texture_pixel_format::rgb" in basic
assert "gl::texture_pixel_format::rgb" in msaa
# Only color0 changes: emissive alpha retains JK decal lighting,
# normal has coverage/classification, and depth/pigment remains float16.
for s in (basic,msaa):
    assert "gl::texture_internal_format::rgba16f" in s
    assert "gl::texture_internal_format::rgb10_a2" in s
    assert "gl::texture_internal_format::r16f" in s
assert "gbuffer_emissive_resolve(screen_res,\n                               gl::texture_internal_format::rgba16f" in msaa
assert "gbuffer_normal_resolve(screen_res,\n                             gl::texture_internal_format::rgb10_a2" in msaa
assert "bool covered=has_opaque_coverage(tc);" in compose
assert "out_color=vec4(emissive.rgb+lit,covered?albedo.a:0.0);" in compose
# GL missing RGB alpha samples as 1, even for cleared regions.
for covered,readback_alpha,expected in ((False,1.,0.),(True,1.,1.),
                                        (False,0.,0.),(True,.99999,.99999)):
    assert (readback_alpha if covered else 0.)==expected
print("Opaque HDR G-buffer packing: config, MSAA matching, baseline and HOM alpha OK")
