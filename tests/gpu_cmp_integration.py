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
assert 'j.contains("enable_native_fallback")' in parser
assert "!the_config->enable_native_fallback" in renderer
assert "jk_cmp_lightlevel_offset==0x330U" in hdr
assert "jk_cmp_is_fullbright" in hdr
assert "std::memcpy(&cmp,sector_state.data()+4U,sizeof(cmp));" in read
assert "jk_cmp_read_tint(read,cmp,current)" in read
assert "native_sector_cmp_tint" in vertex
assert "in vec3 cached_sector_cmp_tint;" in fragment
assert "gl::enable_vertex_attrib_array(7);" in cache
assert "frame_sector_cmp_tint.assign(" in renderer
assert "&cache.frame_sector_cmp_tint))" in renderer
assert "make_span(cache.frame_sector_cmp_tint)" in renderer
assert "jk_cmp_read_lighttable(read_bytes,cmp,levels)" in renderer
assert "out.solid_fullbright=jk_cmp_is_fullbright(" in renderer
assert "prog.set_solid_fullbright(draw.texture.solid_fullbright);" in renderer
assert "shader.set_solid_fullbright(material.solid_fullbright);" in renderer
assert "solid_fullbright==0 && cached_light_state.z>2.5" in fragment
assert "solid_fullbright!=0 ? vec3(1.0) : clamp(lighting,0.0,1.0);" in fragment
assert "jk_fragment_lit=solid_fullbright!=0" in model
assert "out_normal=(world_lighting_exempt!=0 || solid_fullbright!=0)" in model
print("CMP sector tint / solid fullbright wiring checks passed")
