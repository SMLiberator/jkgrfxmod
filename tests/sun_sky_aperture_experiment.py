"""Regression guards: opt-in sunlight only after sky polygon intersection."""
from pathlib import Path
r=Path(__file__).resolve().parents[1]
def read(path):return (r/path).read_text(encoding="utf-8")
cfg=read("common/config.hpp")
parser=read("common/config.cpp")
h=read("renderer/sun_sky_aperture_gpu.hpp")
render=read("renderer/renderer.cpp")
sh=read("renderer/shaders.hpp")
sc=read("renderer/shaders.cpp")
opaque=read("assets/shaders/post_opaque_composite.frag")
shared=read("assets/shaders/translucent_sun_sampling.glsl")
assert "bool experimental_sun_sky_aperture = false;" in cfg
assert 'j.contains("experimental_sun_sky_aperture")' in parser
assert "**experimental_sun_sky_aperture**" in read("doc/config.md")
assert "GL_TEXTURE_2D_ARRAY" in h
assert "GL_DEPTH_COMPONENT24" in h
assert "GL_TEXTURE_COMPARE_MODE,GL_NONE" in h
assert "GL_TEXTURE_BORDER_COLOR,white" in h
assert "sun_shadow_same_projection(view,gate.views[cascade])" in render
assert "sky_aperture_depth_transform" in render
assert "gate.expanded_sky_view(shadow_view)" in render
assert "gate.signatures[cascade]==signature" in render
assert "gate.valid[cascade]=true;" in render
assert "world.load(read,native_world,250000U,true,true,true)" in h
assert "surface.surface_flags&0x600U" in h
assert "flags&0x600U" in render
assert "geo<3U || geo>4U" in render
assert "void draw_sun_sky_aperture(int cascade," in render
assert "draw_sun_sky_aperture(cascade,view,safe_read);" in render
assert "sun_sky_aperture_ready()" in render
assert "glBindTexture(GL_TEXTURE_2D_ARRAY," in render
assert "fragment_texture_units>=20" in render
assert "set_sun_sky_aperture" in sh and "set_sun_sky_aperture" in sc
assert "set_sky_aperture" in sh and "set_sky_aperture" in sc
assert "sun_sky_aperture_active==0)return 1.0;" in opaque
assert "float sky_access=sun_sky_aperture(world_position);" in opaque
assert "0.75*sun_fade*sky_access" in opaque
assert "float sun_sky_aperture_sample(vec3 uv,int layer)" in opaque
assert "translucent_sun_sky_aperture_active==0)return 1.0;" in shared
assert "float translucent_sun_sky_sample(vec3 uv,int layer)" in shared
for path in ("game_opaque_pass.frag","game_trns_pass.frag","gpu_3do_preview.frag"):
    frag=read("assets/shaders/"+path)
    assert "uniform sampler2DArray translucent_sun_sky_aperture_map;" in frag
    assert "fade*translucent_sun_sky_aperture(world)" in frag
for path in ("game_trns_pass.frag","gpu_3do_preview.frag"):
    frag=read("assets/shaders/"+path)
    assert "float sky_access=translucent_sun_sky_aperture(world);" in frag
    assert "if(sky_access<=0.0)return baked;" in frag
    assert "visibility*sky_access*opacity*range_fade;" in frag
def aperture(sky,receiver):
    return sky<0.999999 and sky+0.00001<receiver
assert not aperture(1.0,0.9)
assert not aperture(0.8,0.5)
assert aperture(0.1,0.5)
assert not aperture(0.5,0.5)
for path in ("renderer/renderer.vcxproj","renderer/renderer.vcxproj.filters"):
    assert "sun_sky_aperture_gpu.hpp" in read(path)
print("Opt-in sunlight sky-aperture source contracts passed")

# A disabled feature must not leave sampler2DArray at unit zero, which
# collides with normal 2D samplers and can black out the entire frame.
for name in ("sun_sky_aperture", "sky_aperture"):
    start=sc.index("::set_" + name + "(")
    setter=sc[start:start+600]
    assert setter.index("gl::set_uniform_integer(" + name + "_map,sampler);") < setter.index("if(!active)return;")
assert "glGetIntegerv(GL_MAX_TEXTURE_IMAGE_UNITS,&fragment_units)" in sc
assert "if(sky_aperture)" in sc and 'defines+="#define JKGM_SUN_SKY_APERTURE 1' in sc
assert "#ifdef JKGM_SUN_SKY_APERTURE" in opaque
assert "float sun_sky_aperture(vec3 position) { return 1.0; }" in opaque
assert "float translucent_sun_sky_aperture(vec3 world) { return 1.0; }" in shared
for path in ("game_opaque_pass.frag", "game_trns_pass.frag", "gpu_3do_preview.frag"):
    assert "#ifdef JKGM_SUN_SKY_APERTURE" in read("assets/shaders/" + path)
