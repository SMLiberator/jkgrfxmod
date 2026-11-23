"""Radial fog is an opt-in distance-only change; keep depth ABI unchanged."""
from math import hypot, isclose
from pathlib import Path
import re
import json

root = Path(__file__).resolve().parents[1]
read = lambda name: (root / name).read_text(encoding="utf-8")
cfg = read("common/config.hpp")
parser = read("common/config.cpp")
sample = json.loads(read("assets/jkgm.json"))
docs = read("doc/config.md")
renderer = read("renderer/renderer.cpp")
opaque = read("assets/shaders/post_opaque_composite.frag")
world = read("assets/shaders/world_cache.frag")
model = read("assets/shaders/gpu_3do_preview.frag")
native_opaque = read("assets/shaders/game_opaque_pass.frag")
native_alpha = read("assets/shaders/game_trns_pass.frag")
capture = read("assets/shaders/planar_capture.frag")

assert "bool experimental_radial_fog = false;" in cfg
assert 'j.contains("experimental_radial_fog")' in parser
assert 'get_to(rv->experimental_radial_fog);' in parser
assert sample["experimental_radial_fog"] is False
assert "| `experimental_radial_fog` |" in docs
assert "bool experimental_exponential_fog = false;" in cfg
assert 'j.contains("experimental_exponential_fog")' in parser
assert 'get_to(rv->experimental_exponential_fog);' in parser
assert sample["experimental_exponential_fog"] is False
assert "| `experimental_exponential_fog` |" in docs
# Configuration is deliberately carried in the reserved fog_params.w; no
# extra samplers, scene-depth modifications, or per-material state.
assert renderer.count("the_config->experimental_radial_fog") == 5
assert "bool const radial_fog_active=" in renderer
assert "radial_fog_active && !the_config->enable_spotlights && !sun_lit" in renderer
assert "radial_fog_active?(the_config->experimental_exponential_fog?2.0f:1.0f):0.0f" in renderer
assert renderer.count("the_config->experimental_exponential_fog") == 5
assert "prog.set_projector_camera(" in renderer
for frag in (opaque, world, model, native_opaque, native_alpha):
    assert "fog_params.w>0.5" in frag
    assert "fog_params.z" in frag
    assert "fog_params.w>1.5" in frag
    assert "max(fog_params.y+0.5*max(fog_params.x,0.0),1.0)" in frag
    assert "1.0-exp(-2.302585093*max(" in frag
assert "if(depth > 0.0 && depth < 65500.0)" in opaque
assert "fog_depth*=length(vec3(ndc.x*camera_right.w," in opaque
assert "float fog_fraction=clamp((fog_depth-fog_params.x)/span,0.0,1.0);" in opaque
assert "return camera_origin.xyz+forward_distance*(" in opaque
assert "float depth=32.0/max(gl_FragCoord.w" in world
assert "length(cached_world_position-camera_world_origin)*32.0:depth" in world
assert "float fog_depth=fog_params.w>0.5" in model
assert "length(gpu_view_position())*32.0:view_depth" in model
# MatCap lighting may alter the G/B/A pigment payload; radial fog must
# still preserve linear forward depth in the first (red) channel.
assert re.search(r"out_depth\s*=\s*vec4\s*\(\s*view_depth\s*,", model)
assert "float absorption=fluid_absorption(view_depth);" in model
assert "float absorption=world_fluid_absorption(depth);" in world
for frag in (native_opaque, native_alpha):
    assert "dist=length(vec3(vp_pos.xy*vec2(hscale,hscale/aspect),vp_z));" in frag
    assert "float fog_amount = fog_factor(vp_z);" in frag
    assert "uniform vec2 screen_resolution;" in frag
assert "out_depth = vec4(adj_vp_z, unlit_base_color.rgb);" in native_opaque
assert "opaque_depth,vp_z,ray_ratio,fluid_thickness" in native_alpha
# Reflected capture deliberately retains its special 2-leg fog logic.
assert "capture_fog_fraction(total_depth*plane_fraction)" in capture

# At the center, radial equals forward depth. Toward the edge, it becomes
# strictly greater (same 32x JK units), yielding spherical fog contours.
def radial_native(depth, ndc_x, ndc_y, htan, aspect):
    clip_x, clip_y = ndc_x * depth, ndc_y * depth
    return hypot(clip_x * htan, clip_y * htan / aspect, depth)

def radial_compositor(depth, ndc_x, ndc_y, htan, aspect):
    return depth * hypot(ndc_x * htan, ndc_y * htan / aspect, 1.0)

for depth in (32.0, 80.0, 128.0, 512.0):
    for htan, aspect in ((1.0, 16/9), (0.65, 4/3), (1.4, 2.0)):
        for x,y in ((0.0,0.0),(0.8,0.0),(0.0,0.7),(-0.5,0.7)):
            nr=radial_native(depth,x,y,htan,aspect)
            cr=radial_compositor(depth,x,y,htan,aspect)
            assert isclose(nr,cr,rel_tol=1e-7)
            assert nr>=depth
            if x==0.0 and y==0.0:
                assert isclose(nr,depth)
            else:
                assert nr>depth

# Exponential radial fog has no mathematically clear inner ring, preserves
# the fog opacity cap and approaches 90% of that cap at end when start=0.
from math import exp
def fog_exp(dist,start,end,opacity):
    reach=max(end+0.5*max(start,0.0),1.0)
    return opacity*(1.0-exp(-2.302585093*max(dist,0.0)/reach))
for start,end in ((0.0,100.0),(25.0,100.0),(50.0,150.0)):
    for opacity in (0.2,0.75,1.0):
        values=[fog_exp(d,start,end,opacity) for d in (0,1,10,25,50,100,200,1000)]
        assert isclose(values[0],0.0)
        assert 0.0<values[1]<values[2]<values[3]<values[4]<values[5]<values[6]<values[7]<=opacity
assert isclose(fog_exp(100,0,100,1),0.9,abs_tol=1e-6)
assert fog_exp(20,50,100,1)>0.0  # no hard start threshold
assert fog_exp(20,50,100,1)<fog_exp(20,0,100,1) # start softens reach
print("Experimental radial and exponential fog parity/source contracts passed")
