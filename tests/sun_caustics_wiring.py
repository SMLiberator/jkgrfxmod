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
assert "GL_DEPTH_COMPONENT24" in gpu and "GL_R16F" in gpu
assert "glFramebufferTextureLayer" in gpu
assert "world.load(read,current,250000U,true,false,true)" in gpu
assert "caustic_density_output=shadow_caustic_density;" in shadow
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
assert "exp(-ray_length*(0.08+density*0.4)*sun_caustics_params.w)" in frag
assert "(0.095*sun_caustics_params.y)" in frag
assert "sun_caustics_params.z" in frag
assert "pattern*decay*0.75*sun_caustics_params.x" in frag
assert "seconds*the_config->sun_caustics_speed" in renderer
assert "gl::set_uniform_vector(sun_caustics_params,params);" in shaders
assert "sun_caustics_params" in read("renderer/shaders.hpp")
for field,value in (
    ("sun_caustics_intensity","3.0f"),
    ("sun_caustics_scale","3.0f"),
    ("sun_caustics_spread","1.0f"),
    ("sun_caustics_attenuation","1.0f"),
    ("sun_caustics_speed","1.0f")
):
    assert f"float {field} = {value};" in cfg
    assert f'parse_caustics_number("{field}"' in parser
    assert field in docs
    assert field in read("assets/jkgm.json")
assert "JKGM_SUN_CAUSTICS 1" in shaders
for name,slot,target in (
    ("sun_caustics_depth",17,"texture_array"),
    ("sun_caustics_density",18,"texture_array"),
    ("sun_caustics_pattern",19,"tex2d")
):
    assert '{"%s",%s,sampler_kind::%s}'%(name,slot,target) in registry
assert "no new material fields" in docs
print("Procedural caustics Fluid source and compositor contracts passed")
