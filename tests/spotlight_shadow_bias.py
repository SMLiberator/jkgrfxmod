"""Opt-in geometric grazing bias for projector shadow receivers (no GL required)."""
from math import isclose
from pathlib import Path
import json

root=Path(__file__).resolve().parents[1]
read=lambda p:(root/p).read_text(encoding="utf-8")
opaque=read("assets/shaders/post_opaque_composite.frag")
# Match the shader loader's same-directory include expansion. The actual
# spotlight receiver code lives once in this shared GLSL file.
shared=read("assets/shaders/translucent_spotlight_common.glsl")
include='#include "translucent_spotlight_common.glsl"'
native=read("assets/shaders/game_trns_pass.frag").replace(include,shared)
model=read("assets/shaders/gpu_3do_preview.frag").replace(include,shared)
config=read("common/config.hpp")
parser=read("common/config.cpp")
sample=json.loads(read("assets/jkgm.json"))
shaders_cpp=read("renderer/shaders.cpp")
shaders_hpp=read("renderer/shaders.hpp")
renderer=read("renderer/renderer.cpp")

# Absent config key and explicit zero are backwards compatible.
assert "float spotlight_shadow_angle_bias_scale = 0.0f;" in config
assert sample["spotlight_shadow_angle_bias_scale"]==0.0
assert 'j.contains("spotlight_shadow_angle_bias_scale")' in parser
assert "std::isfinite(scale)" in parser
assert "scale<0.0 || scale>2.0" in parser

# All three receivers use the same geometric facing factor and quadratic
# grazing curve, not the artificial diffuse-light incidence floor.
for name,source,variable in (
    ("opaque",opaque,"spotlight_shadow_angle_bias_scale"),
    ("native translucent",native,"translucent_shadow_angle_bias_scale"),
    ("GPU translucent",model,"translucent_shadow_angle_bias_scale")):
    assert f"uniform float {variable};" in source,name
    assert "float grazing=1.0-clamp(abs(facing),0.0,1.0);" in source,name
    assert f"float world_bias=0.015+0.035*{variable}*" in source,name
    assert "grazing*grazing;" in source,name
    assert "float bias=max(0.000025,derivative*world_bias);" in source,name
    assert "vec2(x,y)-vec2(0.5))*0.7*texel;" in source,name
    assert "return " in source and "0.25;" in source,name

assert "spotlight_shadow_visibility(uv,distance,facing)" in opaque
for source in (native,model):
    assert "translucent_shadow_visibility(uv,distance,facing)" in source
    assert "float facing = abs(dot(normal, normalize(-relative)));" in source
assert "float facing=dot(normal,normalize(-relative));" in opaque

# Uniform upload stays active even when the single shadow slot is -1,
# so enabling the feature next frame cannot inherit a stale scale.
assert shaders_cpp.count("gl::set_uniform_float(translucent_shadow_angle_bias_scale,angle_bias_scale);")==2
assert shaders_cpp.count("gl::set_uniform_float(spotlight_shadow_angle_bias_scale,angle_bias_scale);")==1
assert shaders_hpp.count("void set_translucent_shadow(int slot,float near_plane,float far_plane,")==2
assert "void set_spotlight_shadow(int slot,float near_plane,float far_plane," in shaders_hpp
assert renderer.count("the_config->spotlight_shadow_angle_bias_scale") == 3

# Numerical model: world-space offset increases monotonically only near
# grazing angles, with no reduction at perpendicular illumination. Check
# the perspective-depth mapping at multiple receiver distances.
def world_bias(scale,facing):
    grazing=1.0-min(max(abs(facing),0.0),1.0)
    return 0.015+0.035*scale*grazing*grazing

for scale in (0.0,0.5,1.0,2.0):
    assert isclose(world_bias(scale,1.0),0.015)
    assert isclose(world_bias(scale,-1.0),0.015)
    assert world_bias(scale,0.0)>=world_bias(scale,0.5)>=world_bias(scale,1.0)
    assert world_bias(scale,0.0)<=0.085+1.e-6
    for facing in (0.0,0.2,0.8,1.0):
        for distance in (0.1,1.0,5.0,20.0,40.0):
            near,far=0.05,50.0
            derivative=far*near/((far-near)*distance*distance)
            comparison_bias=max(0.000025,derivative*world_bias(scale,facing))
            assert comparison_bias>=0.000025
            if scale==0.0:
                assert isclose(comparison_bias,
                               max(0.000025,derivative*0.015))
assert isclose(world_bias(1.0,0.0),0.05)
assert isclose(world_bias(0.0,0.0),0.015)
print("Spotlight shadow angle-aware receiver bias regression passed")
