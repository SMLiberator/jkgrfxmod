"""Source contracts for the default-off procedural sun-transmission experiment."""
from pathlib import Path
R=Path(__file__).resolve().parents[1]
read=lambda p:(R/p).read_text(encoding="utf-8")
cfg=read("common/config.hpp")
parser=read("common/config.cpp")
docs=read("doc/sun_caustics.md")
renderer=read("renderer/renderer.cpp")
gpu=read("renderer/sun_caustics_gpu.hpp")
pattern=read("renderer/sun_caustics.hpp")
frag=read("assets/shaders/post_opaque_composite.frag")
shadow=read("assets/shaders/spotlight_shadow.frag")
shaders=read("renderer/shaders.cpp")
registry=read("renderer/texture_units.hpp")
assert "bool experimental_sun_caustics = false;" in cfg
assert 'j.contains("experimental_sun_caustics")' in parser
assert "sun_caustics.hpp" in read("renderer/renderer.vcxproj")
assert "sun_caustics_gpu.hpp" in read("renderer/renderer.vcxproj.filters")
assert "experimental_sun_caustics" in read("doc/config.md")
# Generated exactly once per target construction, never per render call.
assert "generate_sun_caustic_pattern()" in gpu
assert "glGenerateMipmap(GL_TEXTURE_2D)" in gpu
assert "GL_LINEAR_MIPMAP_LINEAR" in gpu
assert "sun_caustic_cell_count" in pattern
# Source-depth + source-fluid coefficient at the same nearest intersection.
assert "GL_DEPTH_COMPONENT24" in gpu and "GL_RGBA16F" in gpu
assert "glFramebufferTextureLayer" in gpu
assert "world.load(read,current,250000U,true,false,true)" in gpu
assert "caustic_density_output=vec4(shadow_caustic_density," in shadow
assert "shadow_caustic_from_texture" in shadow
assert "shadow_caustic_color_factor" in shadow
assert "shadow_caustic_color_blend" in shadow
assert "shadow_caustic_tint" not in shadow
assert "set_caustic_color(textured,next_textured," in renderer
assert renderer.count("hash_fluid_color(mat);")==2
assert "GL_TEXTURE_BINDING_2D,&old_tex11" in renderer
assert "GL_TEXTURE_BINDING_2D,&old_tex12" in renderer
assert "set_caustic_color(bool textured,bool next_textured" in read("renderer/shaders.hpp")
assert "shadow_caustic_to_texture,next_textured?1:0" in shaders
assert "set_caustic_density(draw.coefficient)" in renderer
assert "mat.fluid_thickness" in renderer
assert "mat.blend!=material_blend_mode::fluid" in renderer
assert "inst.is_sprite_quad ||" in renderer
assert "draw_sun_caustics();" in renderer
assert "sun_shadow_same_projection(map.views[cascade],view)" in renderer
assert "sun_caustic_target.reset();" in renderer
# The ordinary sun visibility, sky gate, shadow atlas and fog remain in use.
assert "direct_visibility*caustic" in frag
assert "sun_sky_aperture(world_position)" in frag
assert "sun_dynamic_shadow_visibility(world_position,incidence)" in frag
assert "sun_shadow_visibility(world_position,incidence)" in frag
assert "textureLod(sun_caustics_pattern" in frag
assert "density*ray_length" in frag
assert "CAUSTICS_SHORE_FADE_DISTANCE = 0.35" in frag
assert "SUN_WATER_DIRECT_ATTENUATION = 1.0" in frag
assert "smoothstep(0.0,CAUSTICS_SHORE_FADE_DISTANCE" in frag
assert "sample_sun_fluid_cascade(world_position,0,near_coord)" in frag
assert "sample_sun_fluid_cascade(world_position,1,far_coord)" in frag
assert "smoothstep(transition_start,transition_end,camera_depth)" in frag
assert "submerged_visibility=direct_visibility*water_light.yzw" in frag
assert "SUN_WATER_TINT_STRENGTH = 1.0" in frag
assert "water.gba" in frag
assert "vec3 hue=water_light.yzw/" in frag
assert "float caustic=water_light.x" in frag
assert "CAUSTICS_INTENSITY = 6.0" in frag
assert "CAUSTICS_SCALE = 10.0" in frag
assert "CAUSTICS_SPREAD = 8.0" in frag
assert "CAUSTICS_ATTENUATION = 8.0" in frag
assert "CAUSTICS_SPEED = 1.0" in frag
assert "pattern*decay*CAUSTICS_INTENSITY" in frag
assert "target.set_sun_caustics(true,seconds);" in renderer
assert "set_sun_caustics(bool enabled,float time_seconds)" in read("renderer/shaders.hpp")
for key in ("sun_caustics_intensity","sun_caustics_scale","sun_caustics_spread",
            "sun_caustics_attenuation","sun_caustics_speed"):
    assert key not in cfg and key not in parser and key not in renderer
    assert key not in read("assets/jkgm.json") and key not in read("doc/config.md")
assert "sun_caustics_params" not in frag and "sun_caustics_params" not in shaders
assert "JKGM_SUN_CAUSTICS 1" in shaders
for name,slot,target in (
    ("sun_caustics_depth",17,"texture_array"),
    ("sun_caustics_density",18,"texture_array"),
    ("sun_caustics_pattern",19,"tex2d")
):
    assert '{"%s",%s,sampler_kind::%s}'%(name,slot,target) in registry
assert "no new material fields" in docs
# White materials retain the previous neutral dimming; blue/green water
# progressively attenuates red light without tinting the surface at depth 0.
from math import exp,isclose
def transmission(depth, color, strength=1.0):
    return tuple(exp(-depth*(1+strength*(1-c))) for c in color)
for depth in (0.0,0.1,0.5,2.0):
    assert all(isclose(v,exp(-depth)) for v in
               transmission(depth,(1.0,1.0,1.0)))
    assert all(isclose(v,exp(-depth)) for v in
               transmission(depth,(0.1,0.5,0.9),0.0))
    red,green,blue=transmission(depth,(0.1,0.5,0.9))
    assert red<=green<=blue
    if depth>0.0:assert red<green<blue
print("Procedural caustics Fluid source and compositor contracts passed")
