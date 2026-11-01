"""GPU-cached SPR eligibility/lighting and Thing-offset COG wiring checks.

The patched JKGM native sprite draws ExtraLight from rdSprite+0x8c,
and provides current rdCamera ambient at +0x54. LightModes 2 and 3
deliberately approximate mode 1 while native point lighting is deferred.
"""
from pathlib import Path

root=Path(__file__).resolve().parents[1]
read=lambda p:(root/p).read_text(encoding="utf-8")
main=read("renderer/main.cpp")
quad=read("renderer/sprite_gpu_quad.hpp")
sh=read("assets/shaders/gpu_3do_preview.vert")
offsets=read("renderer/cog_thing_offsets.hpp")

assert "if(lighting>3U)return reject(2)" in main
assert "if(lighting!=0U)return reject(2)" not in main
assert "src+0x8cU" in main
assert "uintptr_t(camera)+0x54U" in main
assert "native_ambient=std::clamp(native_ambient,0.0f,1.0f)" in main
assert "cel,lighting,native_face_extra" in main
assert "variant->second.lighting_mode!=lighting" in main
assert "variant->second.face_extra_light!=native_face_extra" in main
assert "instance.effective_light_mode=lighting>0U?1.0f:0.0f" in main
assert "instance.ambient=native_ambient" in main
assert "v.light_mode=light_mode==0U?0.0f:1.0f" in quad
assert "v.face_extra=extra_light" in quad
assert "max(clamp(l+local_extra_light,0.0,1.0),ambient_light)" in sh
assert 'cog_verb_entry{"getthingoffsetpos", cog_get_thing_offset_pos}' in main
assert 'cog_verb_entry{"getthingposoffset", cog_get_thing_pos_offset}' in main
assert 'getthingrelativepos' not in main
assert "cog_thing_offset_pos(" in offsets
assert "cog_thing_pos_offset(" in offsets
print("SPR LightModes 0-3 and bidirectional Thing offsets wired")
