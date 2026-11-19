"""Independent sRGB MatCap renderer and pack upload wiring."""
from pathlib import Path
r=Path(__file__).resolve().parents[1]
read=lambda path:(r/path).read_text(encoding="utf-8")
compiler=read("compile/main.cpp")
model=read("common/material.hpp")
metadata=read("common/material_map.cpp")
surface=read("renderer/vidmem_texture.hpp")
upload=read("renderer/vidmem_texture.cpp")
renderer=read("renderer/renderer.cpp")
shader_h=read("renderer/shaders.hpp")
shader_cpp=read("renderer/shaders.cpp")
units=read("renderer/texture_units.hpp")
world=read("assets/shaders/world_cache.frag")
three=read("assets/shaders/gpu_3do_preview.frag")
doc=read("doc/material_layers.md")
assert 'v != "matcap"' not in compiler
assert 'v != "shared"' in compiler
assert 'rv->matcap_map=import_matcap_image("matcap_map",".matcap.png",false);' in compiler
assert '"matcap_surface_map",".matcap_surface.png",true' in compiler
assert '"matcap_factor"' in compiler and '"matcap_roughness"' in compiler
assert 'matcap_map' in model and 'matcap_surface_map' in model
assert 'matcap_factor' in metadata and 'matcap_roughness' in metadata
assert 'std::optional<srgb_texture_id> matcap_map' in surface
assert 'std::optional<linear_texture_id> matcap_surface_map' in surface
assert 'get_srgb_texture_from_filename(*(*repl_map)->matcap_map)' in upload
assert 'get_linear_texture_from_filename(' in upload
assert 'get_linear_texture_from_filename(\n                        *(*repl_map)->matcap_surface_map)' in upload
assert 'authored_reflections || default_normals || active_matcap' in upload
assert 'release_srgb_texture(*matcap_map)' in upload
assert 'release_linear_texture(*matcap_surface_map)' in upload
assert 'surface->matcap_map' in renderer
assert 'surface->matcap_surface_map' in renderer
assert 'matcap_available() const' in renderer
assert 'return fragment_units>=26;' in renderer
assert 'bool const matcap_active=matcap_available()' in renderer
assert 'shader.set_matcap(matcap_active,' in renderer
assert 'prog.set_matcap(matcap_active,' in renderer
assert renderer.count('gl::set_active_texture_unit(24);')>=2
assert renderer.count('gl::set_active_texture_unit(25);')>=2
assert 'void set_matcap(bool enabled,point<4,float> params,bool has_surface)' in shader_h
assert 'world_cache_preview_shader::set_matcap(' in shader_cpp
assert 'gpu_3do_preview_shader::set_matcap(' in shader_cpp
for stage in (world,three):
    assert '#ifdef JKGM_MATCAP_LAYER' in stage
    assert 'uniform sampler2D matcap_map,matcap_surface_map;' in stage
    assert 'matcap_enabled==0' in stage
    assert 'matcap_params.rgb*mask.r' in stage
    assert 'roughness*log2' in stage
    assert 'textureLod(matcap_map,map_uv,lod)' in stage
assert 'world_matcap_effect(material_texcoord)' in world
assert 'gpu_matcap_effect(uv)' in three
assert 'out_emissive=vec4(emission+env_reflection+direct_specular+matcap_effect,' in world
assert 'specular+=gpu_matcap_effect(uv)*(1.0-fog);' in three
assert '"matcap_map",24,sampler_kind::tex2d' in units
assert '"matcap_surface_map",25,sampler_kind::tex2d' in units
assert 'units>=26' in shader_cpp
assert 'JKGM_MATCAP_LAYER 1' in shader_cpp
assert 'no specular' not in doc.lower() or 'not as a second GGX lobe' in doc
print("Matcap compiler/runtime upload, GPU world/3DO layers and sampler guard OK")
