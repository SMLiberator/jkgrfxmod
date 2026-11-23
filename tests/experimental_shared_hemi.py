"""Shared Hemi A/B: default-off toggle, capture/sampling parity, profiling."""
from pathlib import Path
import json
root=Path(__file__).resolve().parents[1]
read=lambda p:(root/p).read_text(encoding="utf-8")
conf=read("common/config.hpp")
parser=read("common/config.cpp")
sample=json.loads(read("assets/jkgm.json"))
docs=read("doc/config.md")
cap=read("renderer/hemi_reflection_capture.inc")
rend=read("renderer/renderer.cpp")
world=read("assets/shaders/world_cache.frag")
model=read("assets/shaders/gpu_3do_preview.frag")
hdr=read("renderer/shaders.hpp")
setter=read("renderer/shaders.cpp")
assert sample["experimental_shared_hemi"] is False
assert "bool experimental_shared_hemi = false;" in conf
assert 'j.contains("experimental_shared_hemi")' in parser
assert "**experimental_shared_hemi**" in docs
assert "shared.mode=7;" in cap
assert "(candidate.is_shared && the_config->experimental_shared_hemi)" in cap
assert "int const face_count=paraboloid_mode?2:6;" in cap
assert "int const output_side=paraboloid_mode?side*2:side;" in cap
assert "int const output_layers=paraboloid_mode?2:6;" in cap
assert "global.mode=6;" in cap
assert "entry.mode=selected_mode;" in cap
assert "selected_mode,candidate.is_global,candidate.is_shared," in cap
assert "all_directions_visibility" in cap
assert "active_probe->target->layers()==2?5:reflection_mode" in rend
assert "shader.set_shared_reflection(true,it->second.target->layers()==2);" in rend
assert "if(shared_reflection_ready==2)" in world
assert "for(int hemisphere=0;hemisphere<2;++hemisphere)" in world
assert "vec2 q=vec2(d.x,axis_sign*d.y)/denom;" in world
assert "weighted_color/weighted_coverage" in world
assert "vec3 a=abs(world_dir);" in world
assert "(mode==7 && abs(hemi_capture_info.w-5.0)<0.5)" in model
assert "if(mode==5 || (mode==7 && hemi_capture_info.w<5.5))" in model
assert "else if(mode==6 || mode==7)" in model
assert "void set_shared_reflection(bool ready,bool hemisphere);" in hdr
assert "ready?(hemisphere?2:1):0" in setter
assert "basic_presentation_gpu_profile<1> shared_capture_gpu_queries;" in rend
assert "shared_capture_gpu_queries.begin(shared_gpu_completed)" in cap
assert "shared_capture_gpu_queries.timestamp(0U,true)" in cap
assert "shared_capture_gpu_queries.timestamp(0U,false)" in cap
assert "shared_capture_gpu_queries.end();" in cap
assert "shared_capture_gpu_queries.release();" in rend
assert "PERF shared_capture_gpu_us mode=" in rend
print("Shared Hemi capture and independent GPU A/B profiling wiring OK")
