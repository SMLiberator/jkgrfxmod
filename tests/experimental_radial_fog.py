"""Radial fog is an opt-in distance-only change; keep depth ABI unchanged."""
from math import hypot, isclose
from pathlib import Path
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
# Configuration is deliberately carried in the reserved fog_params.w; no
# extra samplers, scene-depth modifications, or per-material state.
assert renderer.count("the_config->experimental_radial_fog") == 5
assert "bool const radial_fog_active=" in renderer
assert "radial_fog_active && !the_config->enable_spotlights && !sun_lit" in renderer
assert "radial_fog_active?1.0f:0.0f" in renderer
assert "prog.set_projector_camera(" in renderer
for frag in (opaque, world, model, native_opaque, native_alpha):
    assert "fog_params.w>0.5" in frag
    assert "fog_params.z" in frag
assert "if(depth > 0.0 && depth < 65500.0)" in opaque
assert "fog_depth*=length(vec3(ndc.x*camera_right.w," in opaque
assert "fog = clamp((fog_depth - fog_params.x) / span" in opaque
assert "return camera_origin.xyz+forward_distance*(" in opaque
assert "float depth=32.0/max(gl_FragCoord.w" in world
assert "length(cached_world_position-camera_world_origin)*32.0:depth" in world
assert "float fog_depth=fog_params.w>0.5" in model
assert "length(gpu_view_position())*32.0:view_depth" in model
assert "out_depth=vec4(view_depth,albedo.rgb)" in model
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

print("Experimental radial fog parity and source contracts passed")
