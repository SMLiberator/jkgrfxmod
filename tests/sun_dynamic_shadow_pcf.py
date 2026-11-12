"""Four-tap dynamic sunlight atlas PCF: optional and tile-safe on every receiver."""
from pathlib import Path
import json
root=Path(__file__).resolve().parents[1]
read=lambda p:(root/p).read_text(encoding="utf-8")
cfg=read("common/config.hpp")
parser=read("common/config.cpp")
sample=json.loads(read("assets/jkgm.json"))
assert sample["sun_dynamic_shadow_pcf_radius"]==0.0
assert "float sun_dynamic_shadow_pcf_radius = 0.0f;" in cfg
assert 'j.contains("sun_dynamic_shadow_pcf_radius")' in parser
assert "radius<0.0 || radius>2.0" in parser
assert "**sun_dynamic_shadow_pcf_radius**" in read("doc/config.md")
render=read("renderer/renderer.cpp")
assert "uniforms.set_dynamic_pcf_radius(" in render
assert "prog.set_sun_dynamic_pcf_radius(" in render
assert "half_sun_shader->set_sun_dynamic_pcf_radius(" in render
assert render.count("the_config->sun_dynamic_shadow_pcf_radius")==1
assert render.count("effective_sun_dynamic_pcf_radius()")==3
assert "get_sun_settings().intensity.value()[0]" in render
assert "the_config->experimental_sun_pcf_intensity_softness" in render
assert "the_config->experimental_sun_pcf_intensity_reference" in render
helper_cpp=read("renderer/sun_pcf_intensity.hpp")
assert "sun_intensity_dynamic_pcf_radius(" in helper_cpp
assert "std::min(2.0f," in helper_cpp
head=read("renderer/shaders.hpp")
source=read("renderer/shaders.cpp")
assert "gl::uniform_location_id dynamic_pcf_radius;" in head
assert "gl::uniform_location_id sun_dynamic_pcf_radius;" in head
assert "translucent_sun_uniforms::set_dynamic_pcf_radius(" in source
assert "game_post_opaque_composite_shader::set_sun_dynamic_pcf_radius(" in source
assert 'gl::set_uniform_float(dynamic_pcf_radius,radius)' in source
assert 'gl::set_uniform_float(sun_dynamic_pcf_radius,radius)' in source
opaque=read("assets/shaders/post_opaque_composite.frag")
half=read("assets/shaders/post_sun_visibility_half.frag")
translucent=read("assets/shaders/translucent_sun_sampling.glsl")
world=read("assets/shaders/world_cache_direct_specular.glsl")
for path,code in (
    ("opaque",opaque),("half",half),("translucent",translucent),
    ("cached world",world)
):
    assert '#include "sun_dynamic_tile_pcf.glsl"' in code,path
    assert "return sun_dynamic_tile_pcf(" in code or (
        "float object_visibility=sun_dynamic_tile_pcf(" in code
    ),path
for code in (opaque,half):
    assert "uniform float sun_dynamic_pcf_radius;" in code
    assert "sun_dynamic_image,rect,uv,comparison,sun_dynamic_pcf_radius" in code
for code in (translucent,world):
    assert "uniform float translucent_sun_dynamic_pcf_radius;" in code
    assert "comparison,translucent_sun_dynamic_pcf_radius" in code
helper=read("assets/shaders/sun_dynamic_tile_pcf.glsl")
assert "if(radius<=0.0)" in helper
assert "return texture(depth_map,vec3(center/atlas_size,comparison));" in helper
assert "float offset=min(radius,2.0)*0.5;" in helper
assert "vec2 low=tile.xy+vec2(1.5);" in helper
assert "vec2 high=tile.xy+tile.zw-vec2(1.5);" in helper
assert "vec2 sample_pixel=clamp(center+delta,low,high);" in helper
assert "return visibility*0.25;" in helper
assert "for(int y=0;y<2;++y)" in helper
assert "for(int x=0;x<2;++x)" in helper
assert "sun_dynamic_shadow_tile_pcf" not in read("renderer/sun_dynamic_shadow_gpu.hpp")

# Math sanity: each filtered tap is bounded inside the packed tile's
# two-pixel guard; the bilinear sampling footprint cannot reach a neighbor.
for tile_size in (64,128,256,512):
    for tile_x in (0,64,256):
        for u in (0.0,0.001,0.5,0.999,1.0):
            center=tile_x+2.0+u*(tile_size-4.0)
            for radius in (0.5,1.0,1.5,2.0):
                for d in (-radius*0.5,radius*0.5):
                    sampled=max(tile_x+1.5,
                                min(tile_x+tile_size-1.5,center+d))
                    assert sampled>=tile_x+1.5
                    assert sampled<=tile_x+tile_size-1.5
                    # +/-0.5 is the footprint of hardware bilinear.
                    assert sampled-0.5>=tile_x+1.0
                    assert sampled+0.5<=tile_x+tile_size-1.0

# Half-res visibility and full-res fallback must still perform identical
# dynamic shadow calculations (the existing source contract also checks).
def cut(s,start,end):
    return s.split(start,1)[1].split(end,1)[0].strip()
dyn="float sun_dynamic_shadow_visibility("
assert cut(half,dyn,"vec2 encode_oct(")==cut(
    opaque,dyn,"// Only the costly shadow-map lookups")
print("tile-safe dynamic sunlight atlas four-tap PCF contracts passed")
