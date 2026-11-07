"""Fluid alpha-blended sprites/3DOs receive sun/shadows without changing
absorption, dithering or GPU-cached world Fluid behavior.
"""
from pathlib import Path

root = Path(__file__).resolve().parents[1]
def read(path):
    return (root / path).read_text(encoding="utf-8")

native=read("assets/shaders/game_trns_pass.frag")
cached=read("assets/shaders/gpu_3do_preview.frag")
world=read("assets/shaders/world_cache.frag")
alpha=read("renderer/gpu_3do_alpha.hpp")
renderer=read("renderer/renderer.cpp")

# Both forward sprite paths now include regular blending and Fluid, with
# additive/multiply excluded, and no new shadow lookup in the Fluid branch.
nblock=native[native.index("    // Normal and Fluid receive diffuse sunlight"):
              native.index("    vec3 lit = surface_color",native.index(
                  "    // Normal and Fluid receive diffuse sunlight"))]
assert "if((blend_mode==0 || blend_mode==3) && flare_mode==0 &&" in nblock
assert "features.z<0.5)" in nblock
assert nblock.count("surface_color=illuminate_translucent_sun(")==1

gblock=cached[cached.index("    if(transparent_pass!=0 && post_exempt_pass==0 &&"):
              cached.index("    // Decal pigment inherits",cached.index(
                  "    if(transparent_pass!=0 && post_exempt_pass==0 &&"))]
assert "(blend_mode==0 || blend_mode==3)" in gblock
assert "(alpha_mode==1 || alpha_mode==3)" in gblock
for guard in ("world_lighting_exempt==0","decal_receiver_lighting==0",
              "fullbright<0.999","emissive_only==0"):
    assert guard in gblock
assert gblock.count("lit_rgb=illuminate_translucent_sun(")==1

# Native Fluid still attenuates lit diffuse by absorption with alpha
# calculated independently from the same native depth thickness.
assert "float opacity = clamp(albedo.a * absorption, 0.0, 1.0);" in native
assert "vec3 diffuse = (surface_color * (1.0 - fog_amount) +" in native
assert "out_color = vec4(diffuse + specular, opacity);" in native
assert "float opacity=albedo.a*absorption;" in cached
assert "vec3 diffuse=(lit_rgb*(1.0-fog)*vertex_rgb_alpha+" in cached
assert "out_color=vec4(diffuse+emission+specular+projected_light,opacity);" in cached

# Fluid still routes exclusively to alpha; do not allow new dithered
# depth/SSAO behavior or touch world-fluid’s existing lighting path.
assert "if(special_blend || !std::isfinite(opacity))" in alpha
assert alpha.index("if(special_blend || !std::isfinite(opacity))") < alpha.index("if(alpha_dither)")
assert "vec3 lit_diffuse=albedo*lighting*(1.0-env_metalness);" in world
assert "float opacity=coverage*absorption;" in world
assert "out_color=vec4(diffuse+emission+specular,opacity);" in world

# The renderer already enables sun/shadow uniforms for both forwards.
assert "set_translucent_sun(ogs->game_transparency_pass_program,true);" in renderer
assert "set_translucent_sun(prog,!post_exempt && !decals_only);" in renderer
print("Fluid sunlight receiver routing and blending regression passed")
