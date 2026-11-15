"""Source contracts for per-Thing 3DO fades and dithered shadow capture."""
from pathlib import Path
root=Path(__file__).resolve().parents[1]
load=lambda p:(root/p).read_text(encoding="utf-8")
main=load("renderer/main.cpp")
preview=load("renderer/renderer.cpp")
header=load("renderer/gpu_3do_preview.hpp")
state=load("renderer/sprite_instance_info.hpp")
shader=load("assets/shaders/gpu_3do_preview.frag")
shadow=load("assets/shaders/spotlight_shadow.frag")
shaders=load("renderer/shaders.cpp")
sun=load("renderer/sun_shadow.hpp")
for needle in (
    "cog_verb_entry{\"setthingopacity\", cog_set_thing_opacity}",
    "cog_verb_entry{\"getthingopacity\", cog_get_thing_opacity}",
    "thing_opacity_instances.set(",
    "thing_opacity_instances.sample(",
    "inst.thing_opacity=state.opacity;",
    "inst.thing_dither=state.dither;",
    "instance.thing_opacity=thing_fade.opacity;",
    "instance.thing_dither=thing_fade.dither;",
):
    assert needle in main, needle
assert "class thing_opacity_registry" in state
assert "entry.signature==signature" in state
assert "bool dither=false" in state
assert "float thing_opacity=1.0f" in header
assert "bool thing_dither=false" in header
assert "fading_3do && !inst.thing_dither" in preview
assert "fading_3do && inst.thing_dither" in preview
assert "inst.thing_opacity<=0.0f" in preview
assert "inst.thing_opacity) *" in preview
assert "prog.set_thing_opacity(" in preview
assert "emission*=thing_opacity;" in shader
assert "shadow_dither_enabled" in shadow
assert "shadow_instance_opacity" in shadow
assert "const float bayer[16]" in shadow
assert "set_shadow_dither(" in shaders
assert "sun_shadow_model_material_casts_depth(" in sun
assert preview.count("sun_shadow_model_material_casts_depth(")>=3
assert "hash_float(caster.instance->thing_opacity)" in preview
assert "member_float(instance.thing_opacity)" in preview
assert "group_cache->thing_opacity=instance.thing_opacity" in preview
assert "if(d.instance->thing_opacity<0.999f)continue;" in preview
print("Per-Thing 3DO fade COG/GPU/shadow wiring OK")
