"""Guard native CMP separation and indexed-solid fullbright wiring."""
from pathlib import Path
root=Path(__file__).resolve().parents[1]
def load(s):return (root/s).read_text(encoding="utf-8")
read=load("renderer/world_cache_lighting.hpp")
cache=load("renderer/world_cache_gpu.hpp")
renderer=load("renderer/renderer.cpp")
vertex=load("assets/shaders/world_cache_wireframe.vert")
fragment=load("assets/shaders/world_cache_texture_preview.frag")
model=load("assets/shaders/gpu_3do_preview.frag")
hdr=load("renderer/jk_cmp_gpu.hpp")
config=load("common/config.hpp")
parser=load("common/config.cpp")
assert "bool enable_native_fallback = true;" in config
assert "bool restore_cmp_lights = false;" in config
assert 'j.contains("restore_cmp_lights")' in parser
# Exact native JK HW policy: indexed textured faces need the opt-in,
# GeoMode 3 colored faces always use their CMP light-table invariant.
assert ("if(the_config->restore_cmp_lights &&\n"
        "               original_indexed_albedo && tex.mipmap_buffers[mip]!=0U)") in renderer
assert ("bool const indexed_fullbright=geometry_mode==4U &&\n"
        "                the_config->restore_cmp_lights &&") in renderer
assert ("bool const indexed_fullbright=\n"
        "                    the_config->restore_cmp_lights &&\n"
        "                    draw.texture.has_palette_fullbright &&") in renderer
assert "out.solid_fullbright=jk_cmp_is_fullbright(" in renderer
assert "prog.set_solid_fullbright(draw.texture.solid_fullbright);" in renderer
assert "shader.set_solid_fullbright(material.solid_fullbright);" in renderer
assert "if(geometry_mode==3U)shader.set_solid_color(material.tint);" in renderer
# Core CMP fullbright computation is not conditioned on JSON:
# the 8-bit solid-color path always tests the CMP light table.
solid_code=renderer.split("gpu_3do_texture resolve_gpu_3do_solid_material(",1)[1].split(
    "// Diagnostics only: native MAT residency",1)[0]
assert "restore_cmp_lights" not in solid_code
assert 'j.contains("enable_native_fallback")' in parser
assert "!the_config->enable_native_fallback" in renderer
assert "jk_cmp_lightlevel_offset==0x330U" in hdr
assert "jk_cmp_is_fullbright" in hdr
assert "std::memcpy(&cmp,sector_state.data()+4U,sizeof(cmp));" in read
# Identity CMP tint is intentionally bypassed, matching JK rdCache.
# Behavioral zero-vs-identity cases are covered by world_cache.cpp and
# sector_crossing_gpu.cpp; only check the integration symbol here.
assert "jk_cmp_read_effective_tint" in read
assert "jk_cmp_read_effective_tint" in hdr
assert "native_sector_cmp_tint" in vertex
assert "in vec3 cached_sector_cmp_tint;" in fragment
assert "gl::enable_vertex_attrib_array(7);" in cache
assert "frame_sector_cmp_tint.assign(" in renderer
assert "&cache.frame_sector_cmp_tint" in renderer
assert "identity_cmp" in renderer
assert "make_span(cache.frame_sector_cmp_tint)" in renderer
assert "jk_cmp_read_lighttable(read_bytes,cmp,levels)" in renderer
assert "out.solid_fullbright=jk_cmp_is_fullbright(" in renderer
assert "prog.set_solid_fullbright(draw.texture.solid_fullbright);" in renderer
assert "shader.set_solid_fullbright(material.solid_fullbright);" in renderer
# Pixel diffuse is now gated independently of the available native
# point-light list, which can still be needed for optional GGX highlights.
assert "world_pixel_dynamic_lighting!=0 && fullbright<0.999" in fragment
assert "cached_light_state.z>2.5 && num_lights>0" in fragment
assert "lighting=mix(clamp(lighting,0.0,1.0),vec3(1.0),fullbright);" in fragment
assert "vec3 jk_fragment_lit=mix(" in model
assert "out_normal=(world_lighting_exempt!=0 || fullbright>=0.5)" in model
assert "gpu_material_palette_fullbright(" in fragment
assert "gpu_material_palette_fullbright(" in model
assert "shader.set_palette_fullbright(indexed_fullbright);" in renderer
assert "prog.set_palette_fullbright(indexed_fullbright);" in renderer
print("CMP sector tint / per-texel fullbright wiring checks passed")
