"""Default GPU specular is opt-in, low-strength and authored-material safe."""
from pathlib import Path
root=Path(__file__).resolve().parents[1]
read=lambda path:(root/path).read_text(encoding="utf-8")
config=read("common/config.hpp")
parser=read("common/config.cpp")
mat=read("common/material.hpp")
mat_parser=read("common/material_map.cpp")
renderer=read("renderer/renderer.cpp")
world=read("assets/shaders/world_cache_direct_specular.glsl")
model=read("assets/shaders/gpu_3do_preview.frag")

for fragment in (
    "bool enable_default_specular = false;",
    "float default_specular_strength = 0.25f;",
    "float default_specular_roughness = 0.85f;",
):
    assert fragment in config
for name in ("enable_default_specular","default_specular_strength",
             "default_specular_roughness"):
    assert f'j.contains("{name}")' in parser
assert 'if(!std::isfinite(number) || number<0.0 || number>1.0)' in parser
assert 'if(!v.is_number()) {' in parser
assert "bool has_explicit_specular_settings = false;" in mat
assert "mat->has_explicit_specular_settings =" in mat_parser
for name in ("surface_map","reflection_strength","roughness","metalness",
             "environment_map","reflection_mode"):
    assert f'em.contains("{name}")' in mat_parser
assert "bool has_authored_specular=false;" in renderer
assert "surface->replacement_material->has_explicit_specular_settings" in renderer
assert "std::pair<float,float> gpu_specular_parameters(" in renderer
assert "material.reflection_strength<=0.0f &&" in renderer
assert "!material.has_authored_specular" in renderer
assert "return {the_config->default_specular_strength," in renderer
assert "the_config->default_specular_roughness};" in renderer
assert "return {material.reflection_strength,material.roughness};" in renderer
# Both model and world binders reuse the same fallback selection.
assert renderer.count("auto const specular=gpu_specular_parameters(")==2
assert "make_point(specular.first," in renderer
assert "specular.second,draw.texture.metalness," in renderer
assert "make_point(reflective?specular.first:0.0f," in renderer
assert "specular.second,material.metalness," in renderer
# No default specularity for HUD, sprites, decals or exempt/fully-bright VFX.
for name in ("!inst.camera_hud","!inst.is_projected_decal",
             "!inst.is_sprite_quad","!inst.world_lighting_exempt",
             "!draw.texture.solid_fullbright",
             "!draw.texture.emissive_only"):
    assert name in renderer
assert "material.blend!=material_blend_mode::multiply" in renderer
assert "material.blend!=material_blend_mode::additive" in renderer
assert "!material.solid_fullbright" in renderer
assert "!material.emissive_only" in renderer
assert "direct_sun || direct_points" in renderer
assert "world_sun_visibility(cached_world_position" in world
assert "specular_params.x<=0.0" in model
# This roughness/strength pair only changes direct-light specular.
assert "material.has_environment || material.has_environment2 ||" in renderer
print("Experimental GPU default specular opt-in and authoring checks passed")
