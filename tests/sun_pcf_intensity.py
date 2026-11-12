"""Source contracts for optional live sun-intensity PCF radius modulation."""
from pathlib import Path
import json

root=Path(__file__).resolve().parents[1]
read=lambda name:(root/name).read_text(encoding="utf-8")
data=json.loads(read("assets/jkgm.json"))
assert data["experimental_sun_pcf_intensity_softness"]==1.0
assert data["experimental_sun_pcf_intensity_reference"]==2.0
cfg=read("common/config.cpp")
assert 'parse_sun_intensity_pcf("experimental_sun_pcf_intensity_softness"' in cfg
assert 'parse_sun_intensity_pcf("experimental_sun_pcf_intensity_reference"' in cfg
source=read("renderer/renderer.cpp")
assert '#include "sun_pcf_intensity.hpp"' in source
assert "get_sun_settings().intensity.value()[0]" in source
assert "effective_sun_pcf_radii()" in source
assert "effective_sun_dynamic_pcf_radius()" in source
assert "sun_intensity_dynamic_pcf_radius(" in source
assert "uniforms.set_pcf_radius(pcf.near_radius,pcf.far_radius);" in source
assert "prog.set_sun_shadow_pcf_radius(sun_pcf.near_radius," in source
assert "sun_pcf.near_radius,sun_pcf.far_radius" in source
shader=read("assets/shaders/translucent_sun_sampling.glsl")
assert 'if(radius<=0.0)return texture(map,uv);' in shader
assert 'translucent_sun_pcf_radius.x' in shader
assert 'translucent_sun_pcf_radius.y' in shader
assert '#include "sun_dynamic_tile_pcf.glsl"' in shader
assert "sun_dynamic_tile_pcf(" in shader
helper=read("renderer/sun_pcf_intensity.hpp")
assert 'return {near_base,far_base};' in helper
assert 'return {radius(near_base),radius(far_base)};' in helper
assert "inline float sun_intensity_dynamic_pcf_radius(" in helper
assert "return std::min(2.0f," in helper
assert "sun_intensity_pcf_radii(base,base,intensity," in helper
print("intensity-adaptive PCF source contracts OK")
