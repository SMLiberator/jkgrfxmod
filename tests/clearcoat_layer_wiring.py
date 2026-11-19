"""Clearcoat compiled/material/runtime/renderer integration contracts."""
from pathlib import Path
root=Path(__file__).resolve().parents[1]
read=lambda p:(root/p).read_text(encoding="utf-8")
model=read("common/material.hpp")
compiler=read("compile/main.cpp")
video=read("renderer/vidmem_texture.cpp")
videoh=read("renderer/vidmem_texture.hpp")
runtime=read("renderer/runtime_material.hpp")
main=read("renderer/main.cpp")
renderer=read("renderer/renderer.cpp")
capture=read("renderer/hemi_reflection_capture.inc")
shader_h=read("renderer/shaders.hpp")
shader_c=read("renderer/shaders.cpp")
world=read("assets/shaders/world_cache.frag")
direct=read("assets/shaders/world_cache_direct_specular.glsl")
three=read("assets/shaders/gpu_3do_preview.frag")
units=read("renderer/texture_units.hpp")
for prop in ("clearcoat_strength","clearcoat_roughness","clearcoat_transmission",
             "clearcoat_surface_map","clearcoat_normal_map"):
    assert prop in model and prop in compiler
assert "std::array<float,3> clearcoat_defaults" in videoh
assert "register_mat_clearcoat_default" in runtime
assert "register_mat_clearcoat_default(" in renderer
assert "std::optional<animated_floats<3>> clearcoat;" in runtime
assert "em.clearcoat->is_fading(now)" in runtime
assert "authored_clearcoat_.clear();" in runtime
assert "entry.clearcoat->fade_to" in runtime
assert "it->second.clearcoat->fade_to" in runtime
assert "clearcoat_reset=true" in runtime
for verb in ("setmatclearcoat","getmatclearcoat","resetmatclearcoat"):
    assert f'cog_verb_entry{{"{verb}"' in main
assert "cog_push_vector(ctx,current.data())" in main
assert "cog_pop_vector(ctx,values)" in main
assert "cog_pop_float(ctx)" in main
assert "cog_pop_int(ctx)" in main
assert "surf->clearcoat_surface_map" in video
assert "surf->clearcoat_normal_map" in video
assert "release_linear_texture(*clearcoat_surface_map)" in video
assert "release_linear_texture(*clearcoat_normal_map)" in video
assert "clearcoat_available() const" in renderer
assert "return fragment_units>=28;" in renderer
assert "shader.set_clearcoat(coat_active," in renderer
assert "prog.set_clearcoat(coat_active," in renderer
assert 'gl::set_active_texture_unit(26);' in renderer
assert 'gl::set_active_texture_unit(27);' in renderer
assert 'shared_requested=wants_shared(cache.sector_candidates)' in capture
assert 'get<0>(mat.clearcoat_params)>0.0f' in capture
assert 'get<0>(it->second.clearcoat_params)>0.0f' in capture
assert 'JKGM_CLEARCOAT_LAYER 1' in shader_c
assert 'units>=28' in shader_c
for stage in (world,three):
    assert "#ifdef JKGM_CLEARCOAT_LAYER" in stage
    assert "uniform sampler2D clearcoat_surface_map,clearcoat_normal_map;" in stage
    assert "uniform int clearcoat_enabled,clearcoat_has_surface,clearcoat_has_normal;" in stage
    assert "clamp(clearcoat_params.z*pixel.b,0.0,1.0)" in stage
    assert "mix(1.0,coat.z,coat.x)" in stage or "mix(1.0,coat_values.z,coat_values.x)" in stage
    assert "coat_specular" in stage
    assert "base_specular_trans" in stage
assert 'world_clearcoat_normal(material_texcoord)' in world
assert 'world_direct_specular(\n                albedo,material_texcoord,true,N,coat.y,coat.x)' in world
assert "vec3 F0=coating?vec3(0.04)" in direct
assert "gpu_clearcoat_local_normal(uv)" in three
assert "gpu_sun_specular(\n                albedo.rgb,uv,true,coat_local_normal," in three
assert "vec3 F0=coating?vec3(0.04)" in three
assert 'clearcoat_surface_map",26,sampler_kind::tex2d' in units
assert 'clearcoat_normal_map",27,sampler_kind::tex2d' in units
assert "renderer->" not in runtime  # engine-side state stays GL-free
print("Clearcoat rendering, capture reuse, material compiler and COG contracts OK")
