"""Check that COG spotlights illuminate surfaces even in complete sun shadow.

Shader source checks defend all three receiver paths; CPU reference arithmetic
demonstrates the intended independence from directional shadow visibility.
This test requires no GL context.
"""
from pathlib import Path
from math import isclose

root = Path(__file__).resolve().parent.parent
opaque = (root/"assets/shaders/post_opaque_composite.frag").read_text(encoding="utf-8")
native_alpha = (root/"assets/shaders/game_trns_pass.frag").read_text(encoding="utf-8")
gpu_alpha = (root/"assets/shaders/gpu_3do_preview.frag").read_text(encoding="utf-8")
renderer = (root/"renderer/renderer.cpp").read_text(encoding="utf-8")

# Opaque world and GPU 3DOs share this composite: the sun-shadow reduction
# acts on baked/ambient scene pigment, then a separate spotlight diffuse
# and additive term is evaluated without multiplying sun visibility.
opaque_main=opaque.split("void main()",1)[1]
assert "project_spotlights(depth,tc,light_rgb,add_rgb);" in opaque_main
assert "surface_rgb*=1.0-shadowed_baseline*(1.0-visibility);" in opaque_main
assert "vec3 sun_light=pigment*sun_shadow_color_intensity.rgb*" in opaque_main
spotlight_composite=opaque_main.split("    vec3 diffuse_light=pigment*light_rgb;",1)[1]
assert "vec3 screened=surface_rgb+diffuse_light*" in spotlight_composite
assert "vec3 lit=mix(screened+add_rgb,fog_color,fog);" in spotlight_composite
assert "visibility" not in spotlight_composite.split("    out_color=",1)[0]

# Spotlight's OWN occlusion map still applies, but never sunlight's mask.
spot_func=opaque.split("void project_spotlights(",1)[1].split("\n}",1)[0]
assert "shadow=spotlight_shadow_visibility(uv,distance);" in spot_func
assert "diffuse+=radiance*spotlight_intensities[i].x;" in spot_func
assert "additive+=radiance*spotlight_intensities[i].y;" in spot_func
assert "sun_shadow_visibility" not in spot_func

# Native translucent materials and GPU-cached alpha faces:
# Sunlight may darken preexisting diffuse, but projected light must be added
# AFTER that attenuation and cannot be multiplied by sun visibility.
assert native_alpha.index("surface_color=illuminate_translucent_sun(") < (
    native_alpha.index("projected_light = light_translucent_spotlight("))
assert "lit + projected_light;" in native_alpha
native_spot=native_alpha.split("vec3 light_translucent_spotlight(",1)[1].split("\n}",1)[0]
assert "translucent_sun_visibility" not in native_spot
assert gpu_alpha.index("lit_rgb=illuminate_translucent_sun(") < (
    gpu_alpha.index("projected_light=light_translucent_spotlight("))
assert "rgb*a+emission+specular*a+projected_light" in gpu_alpha
gpu_spot=gpu_alpha.split("vec3 light_translucent_spotlight(",1)[1].split("\n}",1)[0]
assert "translucent_sun_visibility" not in gpu_spot

# Both forward passes are wired for direct sun and independent spotlights.
assert "set_translucent_spotlights(ogs->game_transparency_pass_program,true);" in renderer
assert "set_translucent_sun(ogs->game_transparency_pass_program,true);" in renderer
assert "set_translucent_spotlights(prog,transparent && !post_exempt &&" in renderer
assert "set_translucent_sun(prog,!post_exempt && !decals_only);" in renderer

# Reference for opaque composite: zero sun visibility can darken the
# baseline, but cannot darken the separate projector. This is the source
# of the gameplay behavior required for shadowed headlights/flashlights.
def lit_with_spot(sun_visibility, pigment, base, spot_light, spot_add):
    # Normal incidence and full sun range; baseline subtraction is 0.6.
    base_shadowed=base*(1.0-0.6*(1.0-sun_visibility))
    sun_light=pigment*0.5*sun_visibility
    base_sun=base_shadowed+sun_light*(1.0-min(max(base_shadowed,0.0),1.0))
    spot_diffuse=pigment*spot_light
    return (base_sun + spot_diffuse*(1.0-min(max(base_sun,0.0),1.0))
            + spot_add)
for visibility in (0.0,0.25,0.5,1.0):
    no_spot=lit_with_spot(visibility,0.8,0.2,0.0,0.0)
    with_spot=lit_with_spot(visibility,0.8,0.2,0.3,0.1)
    assert with_spot>no_spot
    assert isclose(with_spot-no_spot,0.8*0.3*(1.0-min(max(no_spot,0),1.0))+0.1)
# In the full shadow, an independent spotlight brightens the receiver.
assert lit_with_spot(0.0,0.8,0.2,0.3,0.1)>lit_with_spot(0.0,0.8,0.2,0.0,0.0)
print("Sun shadow / independent spotlight lighting checks passed")
