"""Guard the JKGM Thing RGB parser hooks and single-native-light GPU wiring.
Run tests/thing_light_rgb.cpp for pure numeric/lifecycle assertions.
The binary prologues are taken from jkgm(5).exe, NOT OpenJKDF2.
"""
from pathlib import Path
root = Path(__file__).resolve().parents[1]
main = (root/"renderer/main.cpp").read_text(encoding="utf-8")
renderer = (root/"renderer/renderer.cpp").read_text(encoding="utf-8")
header = (root/"renderer/thing_light_rgb.hpp").read_text(encoding="utf-8")
gpu = (root/"renderer/gpu_3do_preview.hpp").read_text(encoding="utf-8")
world = (root/"renderer/world_cache_lighting.hpp").read_text(encoding="utf-8")
program = (root/"renderer/shaders.cpp").read_text(encoding="utf-8")
model_shader = (root/"assets/shaders/gpu_3do_preview.frag").read_text(encoding="utf-8")
world_specular = (root/"assets/shaders/world_cache_direct_specular.glsl").read_text(encoding="utf-8")

# Both native parser entrypoints, plus the Thing SetThingBasedOn clone,
# are verified before detouring. Inheritance must share the SPR clone hook.
assert "light_rgb_parse_addr=0x004CEE70U" in main
assert "light_rgb_template_addr=0x004DE1C0U" in main
assert "0x8b,0x0d,0xf0,0x26,0x83,0x00,0x53,0x8b" in main
assert "0xa1,0xe8,0x07,0x86,0x00,0x81,0xec,0xb0,0x02,0x00,0x00" in main
assert 'std::memcmp(reinterpret_cast<void const *>(light_rgb_parse_addr)' in main
assert 'std::memcmp(reinterpret_cast<void const *>(light_rgb_template_addr)' in main
assert 'std::memcmp(reinterpret_cast<void const *>(sprite_clone_thing_addr)' in main
assert "std::strtof(cursor,&end)" in main
assert "if(*cursor!=(i<2U?'/':')'))return false;" in main
assert "std::sscanf(" not in main
assert "request_native_thing_light_rgb_hooks();" in main
assert "HookedThingTemplateParse" in main and "HookedThingParseArg" in main
assert "if(active_light_rgb_template.active)" in main
assert "registry.prepare_world(reinterpret_cast<uintptr_t>(world));" in main
assert "registry.prepare_static_world(reinterpret_cast<uintptr_t>(world));" in main
assert "bool const static_template=(world_flags&1U)!=0U" in main
assert "static_world!=0U && static_world==uintptr_t(world)" in main
assert "THING LIGHT RGB template: static=" in main
assert "light_rgb_clock_now(),static_template" in main
assert "if(!sprite_variant_hooks_active)" in main
assert "light_rgb_template_ancestor(" in main
assert "bool have=registry.get(rgb_source," in main
assert "if(have)registry.inherit(" in main
assert "else registry.erase(reinterpret_cast<uintptr_t>(thing));" in main
assert "thing_light_rgb_hooks_active=false;" in main
assert "thing_light_clone_hook_active=false;" in main

# COG's argument order is Thing, RGB vector, fadeSeconds; stack pops reverse.
assert 'cog_verb_entry{"setthinglightrgb", cog_set_thing_light_rgb}' in main
assert 'cog_verb_entry{"getthinglightrgb", cog_get_thing_light_rgb}' in main
assert "float const fade=cog_pop_float(ctx);" in main
assert "cog_pop_vector(ctx,components);" in main
assert "void *thing=cog_pop_thing(ctx);" in main
assert "cog_push_vector(ctx,rgb.data());" in main

# The original native Thing light intensity, position, and owner signature
# are read without changing any SithThing field. Only renderer-side RGB.
assert "entry.address+0x118U,&intensity" in renderer
assert "entry.address+0xb8U,position.data(),12U" in renderer
assert "signature!=entry.signature" in renderer
assert "(flags&1U)==0U" in renderer
assert "find_unique_native_light(" in renderer
assert "frame_saber_light_colors.valid[id]" in renderer
assert "merge_thing_light_colors();" in renderer
assert "get_registry().consume();" in renderer
assert "out.native_id=id;" in main
assert "light.native_id=id;" in world
assert "uint32_t native_id=0" in gpu
assert "light_color.push_back(" in program
assert "std::vector<record> things(double now)" in header
assert "void prepare_static_world(uintptr_t world)" in header
assert "if(it->second.static_template)++it;" in header
assert "v.static_template=is_template && static_template;" in header
assert "static_world_=world;" in header
# RGB must tint both dynamic diffuse and GGX specular; no white highlights.
assert "radiance*light_color[i]" in model_shader
assert "radiance*light_color[i]" in world_specular
assert "double duration=0" in header
assert "std::max({in[0],in[1],in[2]})" in header
