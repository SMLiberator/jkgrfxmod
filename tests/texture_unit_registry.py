"""Source contract: shader sampler assignments must match the C++ registry."""
from collections import defaultdict
from pathlib import Path
import re
R=Path(__file__).resolve().parents[1]
read=lambda p:(R/p).read_text(encoding="utf-8")
header=read("renderer/texture_units.hpp")
programs=("gpu_3do_preview","world_cache","game_opaque_pass",
          "game_trns_pass","post_opaque_composite")
kinds={"tex2d":{"sampler2D"},"shadow":{"sampler2DShadow"},
       "buffer":{"samplerBuffer"},
       "texture_array":{"sampler2DArray"},
       "depth_variant":{"sampler2D","sampler2DShadow"}}
allow={("gpu_3do_preview",frozenset(("displacement_map","decal_light_texture"))),
       ("gpu_3do_preview",frozenset(("palette_fullbright_mask","decal_occlusion")))}
def expand(name,visited=frozenset()):
    assert name not in visited,name
    s=read("assets/shaders/"+name)
    return re.sub(r'(?m)^\s*#include\s+"([^"]+)"',
        lambda m:expand(m.group(1),visited|{name}),s)
def validate(name,mapping,source):
    decl=defaultdict(set)
    for kind,names in re.findall(r"\buniform\s+(sampler\w+)\s+([^;]+);",source):
        for namepart in names.split(","):
            m=re.fullmatch(r"\s*([A-Za-z_]\w*)\s*(?:\[[^\]]+\])?\s*",namepart)
            assert m,(name,namepart)
            decl[m.group(1)].add(kind)
    assert decl.keys()==mapping.keys(),(name,
       "unregistered",sorted(decl.keys()-mapping.keys()),
       "stale",sorted(mapping.keys()-decl.keys()))
    used=defaultdict(list)
    for uniform,(slot,target) in mapping.items():
        assert target in kinds and decl[uniform]==kinds[target],(name,uniform,decl[uniform],target)
        assert 0<=slot<=31,(name,uniform,slot)
        used[slot].append((uniform,target))
    for unit,contents in used.items():
        for i,(first,first_kind) in enumerate(contents):
            for second,second_kind in contents[i+1:]:
                assert first_kind==second_kind,(name,unit,first,second,"sampler target collision")
                assert (name,frozenset((first,second))) in allow,(name,unit,first,second,"undocumented alias")
for prog in programs:
    m=re.search(r"constexpr binding "+prog+r"\[\]=\{(.*?)\n\};",header,re.S)
    assert m,prog
    entries=re.findall(r'\{"(\w+)",(\d+),sampler_kind::(\w+)\}',m.group(1))
    mapping={n:(int(slot),kind) for n,slot,kind in entries}
    assert len(mapping)==len(entries),("duplicate",prog)
    validate(prog,mapping,expand(prog+".frag"))
assert "forward_sector_buffer=19;" in header
assert "forward_next_extended_unit=20;" in header
assert "forward_next_extended_required_units=21;" in header
assert "opaque_sky_aperture=16;" in header
assert "translucent_sun_sky_aperture_map" in header
assert "sun_sky_aperture_map" in header
sh=read("renderer/shaders.cpp")
assert "JKGM_SUN_SKY_APERTURE 1" in sh
assert "units>=texture_units::forward_next_extended_required_units" in sh
assert "static_assert(no_incompatible_target_aliases(gpu_3do_preview)" in header
gpu=read("renderer/gpu_3do_preview.hpp")
cpp=read("renderer/renderer.cpp")
sh=read("renderer/shaders.cpp")
assert "texture_units::forward_sector_buffer;" in gpu
assert "prog.set_decal_receiver_lighting(decals_only," in cpp
assert "texture_units::forward_decal_light" in cpp
for signature in ("game_pass_shader::set_translucent_shadow(",
                  "gpu_3do_preview_shader::set_translucent_shadow("):
    block=sh.split(signature,1)[1].split("\n}",1)[0]
    assert block.index("texture_units::forward_spotlight_shadow")<block.index("if(slot<0)return;")
block=sh.split("game_post_opaque_composite_shader::set_spotlight_shadow(",1)[1].split("\n}",1)[0]
assert block.index("gl::set_uniform_integer(spotlight_shadow_image,10);")<block.index("if(slot>=0)")
# Test that introducing an unregistered sampler fails source validation.
m=re.search(r"constexpr binding gpu_3do_preview\[\]=\{(.*?)\n\};",header,re.S)
entries=re.findall(r'\{"(\w+)",(\d+),sampler_kind::(\w+)\}',m.group(1))
mapping={n:(int(slot),kind) for n,slot,kind in entries}
try:
    validate("gpu_3do_preview",mapping,expand("gpu_3do_preview.frag")+
             "\nuniform sampler2DArray future_aperture;\n")
except AssertionError:
    pass
else:
    raise AssertionError("New unregistered sampler was accepted")
print("OpenGL texture-unit registry, alias and shader declarations passed")

# Core linked-program construction must initialize every registered
# sampler, including those whose feature flag is currently disabled.
assert "initialize_registered_samplers(name,prog);" in sh
assert "glGetIntegerv(GL_CURRENT_PROGRAM,&previous);" in sh
assert "glUseProgram(static_cast<GLuint>(previous));" in sh
assert "glUniform1i(location,entry.unit);" in sh

# Decal light and displacement reuse unit 9 ONLY when mutually exclusive.
assert "bool const parallax=!inst.camera_hud &&\n                    !inst.is_projected_decal &&" in cpp
