"""GPU world direct GGX lights share 3DO materials without requiring EnvMaps."""
from pathlib import Path
root=Path(__file__).resolve().parents[1]
load=lambda x:(root/x).read_text(encoding="utf-8")
fragment=load("assets/shaders/world_cache.frag")
lighting=load("assets/shaders/world_cache_direct_specular.glsl")
renderer=load("renderer/renderer.cpp")
shaders=load("renderer/shaders.cpp")
header=load("renderer/shaders.hpp")
assert '#include "world_cache_direct_specular.glsl"' in fragment
assert "world_direct_specular(albedo,material_texcoord)" in fragment
assert "emission+env_reflection+direct_specular" in fragment
assert "(env_reflection+direct_specular)*(1.0-fog)" in fragment
assert "if(world_pixel_dynamic_lighting!=0" in fragment
assert "vec3 world_ggx(" in lighting
assert "world_sun_visibility(cached_world_position" in lighting
assert "translucent_sun_dynamic_depth" in lighting
assert "world_dynamic_light_specular!=0" in lighting
assert "solid_fullbright!=0" in lighting
assert "void set_specular_lighting(bool pixel_diffuse,bool point_specular)" in header
assert "translucent_sun_uniforms translucent_sun;" in header
assert "translucent_sun(the_program)" in shaders
assert "set_translucent_sun(shader,the_config->enable_reflections)" in renderer
assert "material.has_environment2 ||" in renderer
assert "direct_sun || direct_points" in renderer
assert "shader.set_specular_lighting(pixel_lighting,point_specular);" in renderer
assert "read_world_cache_camera_lights(" in renderer
print("World-cache sun/Thing GGX integration and dynamic diffuse gate checks passed")
