"""Opt-in sun cascade filtering and independent far depth resolution."""
from pathlib import Path
root=Path(__file__).resolve().parents[1]
read=lambda p:(root/p).read_text(encoding="utf-8")
cfg=read("common/config.hpp")
parser=read("common/config.cpp")
gpu=read("renderer/sun_shadow_gpu.hpp")
render=read("renderer/renderer.cpp")
shade=read("renderer/shaders.cpp")
head=read("renderer/shaders.hpp")
opaque=read("assets/shaders/post_opaque_composite.frag")
shared=read("assets/shaders/translucent_sun_sampling.glsl")
docs=read("doc/config.md")
assert "unsigned int sun_shadow_far_resolution = 0U;" in cfg
for key in ("sun_shadow_near_pcf_radius","sun_shadow_far_pcf_radius"):
    assert f"float {key} = 0.0f;" in cfg
    assert f'parse_sun_pcf_radius("{key}"' in parser
    assert f"**{key}**" in docs
assert "sun_shadow_far_resolution must be 0 or between 64 and 4096" in parser
assert 'j.contains("sun_shadow_far_resolution")' in parser
assert "std::isfinite(radius)" in parser
assert "radius<0.0 || radius>4.0" in parser
assert "far_resolution=far_pixels==0U?resolution:" in gpu
assert "make_size(int(far_resolution),int(far_resolution))" in gpu
assert "setup_static(static_fbo,static_depth,resolution)" in gpu
assert "static_far_fbo,static_far_depth,far_resolution" in gpu
assert "the_config->sun_shadow_far_resolution" in render
assert "stabilize_sun_shadow_projection(view,cascade_resolution)" in render
assert "int(cascade_resolution),int(cascade_resolution)" in render
assert "sun_shadow_views[cascade]=view;" in render
assert "the_config->sun_shadow_near_distance" in render
assert "the_config->sun_shadow_far_distance" in render
assert "gl::blit_framebuffer(" in render
assert "sun_shadow_pcf_radius" in head
assert "sun_shadow_pcf_radius" in shade
assert "set_sun_shadow_pcf_radius(" in render
assert "uniform vec2 sun_shadow_pcf_radius;" in opaque
assert "if(radius<=0.0)return texture(map,lookup);" in opaque
assert "sun_shadow_pcf_radius.x" in opaque
assert "sun_shadow_pcf_radius.y" in opaque
assert "return sum*0.25;" in opaque
assert "translucent_sun_uniforms::set_pcf_radius" in shade
assert "if(radius<=0.0)return texture(map,uv);" in shared
assert "translucent_sun_pcf_radius.x" in shared
assert "translucent_sun_pcf_radius.y" in shared
assert "return texture(translucent_sun_dynamic_depth," in shared
for path in ("assets/shaders/game_opaque_pass.frag",
             "assets/shaders/game_trns_pass.frag",
             "assets/shaders/gpu_3do_preview.frag"):
    src=read(path)
    assert "uniform vec2 translucent_sun_pcf_radius;" in src,path
    assert '#include "translucent_sun_sampling.glsl"' in src,path
print("Separate far map and matching opaque/translucent PCF checks passed")
