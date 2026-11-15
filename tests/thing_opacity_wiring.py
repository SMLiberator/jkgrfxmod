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
    "inst.thing_opacity=active_native_thing_opacity.opacity;",
    "inst.thing_dither=active_native_thing_opacity.dither;",
    "active_native_thing_opacity=previous_thing_opacity;",
    "auto const thing_fade=sample_3do_opacity(",
    "instance.thing_opacity=thing_fade.opacity;",
    "instance.thing_dither=thing_fade.dither;",
):
    assert needle in main, needle
# Sampling happens once per native model draw and once per independent
# reflected Thing, never once for each hierarchy mesh.
assert main.count("auto const thing_fade=sample_3do_opacity(")==1
assert "uintptr_t(hud_owner)+0xc4U==native_rd" in main
assert "clear_thing_opacity(); // levels may recycle Thing pointers/signatures" in main
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
capture=load("assets/shaders/planar_capture.frag")
planar=load("renderer/planar_reflection_capture_shader.hpp")
assert "capture_thing_mode" in capture and "capture_thing_opacity" in capture
assert "out_color*=capture_thing_opacity;" in capture
assert "set_thing_fade(float opacity,int mode)" in planar
assert "std::vector<planar_emissive_model_group> faded_models;" in preview
assert "shader.set_thing_fade(inst.thing_opacity,1)" in preview
assert "shader.set_thing_fade(inst.thing_opacity," in preview
assert "glBlendFuncSeparate(GL_ONE,GL_ONE_MINUS_SRC_ALPHA," in preview
assert "set_shadow_dither(" in shaders
assert "cached_dither_enabled_!=enabled" in shaders
assert "sun_shadow_model_material_casts_depth(" in sun
assert preview.count("sun_shadow_model_material_casts_depth(")>=3
assert "hash_float(caster.instance->thing_opacity)" in preview
assert "member_float(instance.thing_opacity)" in preview
assert "group_cache->thing_opacity=instance.thing_opacity" in preview
assert "if(d.instance->thing_opacity<0.999f)continue;" in preview
print("Per-Thing 3DO fade COG/GPU/shadow wiring OK")
