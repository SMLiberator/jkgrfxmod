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
       "buffer":{"samplerBuffer","usamplerBuffer"},
       "texture_array":{"sampler2DArray"}}
allow={("gpu_3do_preview",frozenset(("displacement_map","decal_light_texture"))),
       ("gpu_3do_preview",frozenset(("palette_fullbright_mask","decal_occlusion")))}
def expand(name,visited=frozenset()):
    assert name not in visited,name
    s=read("assets/shaders/"+name)
    return re.sub(r'(?m)^\s*#include\s+"([^"]+)"',
        lambda m:expand(m.group(1),visited|{name}),s)
def validate(name,mapping,source):
    decl=defaultdict(set)
    for kind,names in re.findall(r"\buniform\s+([iu]?sampler\w+)\s+([^;]+);",source):
        for namepart in names.split(","):
            m=re.fullmatch(r"\s*([A-Za-z_]\w*)\s*(?:\[[^\]]+\])?\s*",namepart)
            assert m,(name,namepart)
            decl[m.group(1)].add(kind)
    assert decl.keys()==mapping.keys(),(name,
       "unregistered",sorted(decl.keys()-mapping.keys()),
       "stale",sorted(mapping.keys()-decl.keys()))
    used=defaultdict(list)
    for uniform,(slot,target) in mapping.items():
        assert target in kinds,(name,uniform,target)
        if target=="buffer":
            assert decl[uniform] in ({"samplerBuffer"},{"usamplerBuffer"}),(name,uniform,decl[uniform])
        else:
            assert decl[uniform]==kinds[target],(name,uniform,decl[uniform],target)
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
    source=expand(prog+".frag")
    if prog in ("gpu_3do_preview","world_cache"):
        source+=expand(prog+".vert")
    validate(prog,mapping,source)
# Ensure new, unregistered sampler declarations are rejected.
# Test that introducing an unregistered sampler fails source validation.
m=re.search(r"constexpr binding gpu_3do_preview\[\]=\{(.*?)\n\};",header,re.S)
entries=re.findall(r'\{"(\w+)",(\d+),sampler_kind::(\w+)\}',m.group(1))
mapping={n:(int(slot),kind) for n,slot,kind in entries}
try:
    validate("gpu_3do_preview",mapping,
             expand("gpu_3do_preview.frag")+
             expand("gpu_3do_preview.vert")+
             "\nuniform sampler2DArray future_aperture;\n")
except AssertionError:
    pass
else:
    raise AssertionError("New unregistered sampler was accepted")
print("OpenGL sampler registry and declared-target compatibility passed")
