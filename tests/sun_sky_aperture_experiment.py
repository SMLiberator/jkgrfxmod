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
assert "texture_units::forward_next_extended_required_units" in render
assert "texture_units::forward_next_extended_unit" in render
assert "glActiveTexture(GL_TEXTURE0+19)" not in render
assert "#ifdef JKGM_SUN_SKY_APERTURE" in opaque
assert "#ifdef JKGM_SUN_SKY_APERTURE" in shared
assert "set_sun_sky_aperture" in sh and "set_sun_sky_aperture" in sc
assert "set_sky_aperture" in sh and "set_sky_aperture" in sc
assert "sun_sky_aperture_active==0)return 1.0;" in opaque
assert "float sun_sky_aperture(vec3 position) { return 1.0; }" in opaque
assert "float sky_access=sun_sky_aperture(world_position);" in opaque
assert "float direct_visibility=visibility*sky_access;" in opaque
assert "float sun_applied=incidence*sun_fade;" in opaque
assert "sprite ? 0.75*sun_fade" in opaque
assert "0.75*sun_fade*sky_access" not in opaque
assert "1.0-direct_visibility" in opaque
assert "sun_applied*direct_visibility" in opaque
assert "float sun_sky_aperture_sample(vec3 uv,int layer)" in opaque
assert "translucent_sun_sky_aperture_active==0)return 1.0;" in shared
assert "float translucent_sun_sky_aperture(vec3 world) { return 1.0; }" in shared
assert "float translucent_sun_sky_sample(vec3 uv,int layer)" in shared
world=read("assets/shaders/world_cache_direct_specular.glsl")
assert "#ifdef JKGM_SUN_SKY_APERTURE" in world
assert "uniform sampler2DArray translucent_sun_sky_aperture_map;" in world
assert "world_sun_sky_aperture(cached_world_position)" in world
assert "float world_sun_sky_aperture(vec3 world) { return 1.0; }" in world
for path in ("game_opaque_pass.frag","game_trns_pass.frag","gpu_3do_preview.frag"):
    frag=read("assets/shaders/"+path)
    assert "uniform sampler2DArray translucent_sun_sky_aperture_map;" in frag
    assert "#ifdef JKGM_SUN_SKY_APERTURE" in frag
    assert "fade*translucent_sun_sky_aperture(world)" in frag
for path in ("game_trns_pass.frag","gpu_3do_preview.frag"):
    frag=read("assets/shaders/"+path)
    assert "float sky_access=translucent_sun_sky_aperture(world);" in frag
    assert "if(sky_access<=0.0)return baked;" not in frag
    assert "float direct_visibility=visibility*sky_access;" in frag
    assert "(1.0-direct_visibility)" in frag
    assert "incidence*direct_visibility*opacity*range_fade;" in frag
# Shared depth comparisons MUST precede filtering: linear interpolation
# of depth would invent sky access at solid/sky transitions.
filter_source=read("assets/shaders/sun_sky_aperture_filter.glsl")
assert "texelFetch(aperture_map,ivec3(at,layer),0).r" in filter_source
assert "GL_LINEAR" not in filter_source
assert "texture(aperture_map" not in filter_source
assert "for(int y=0;y<2;++y)" in filter_source
assert "for(int x=0;x<2;++x)" in filter_source
assert "smoothstep(0.25,1.0,coverage)" in filter_source
for shader in (opaque,shared,world):
    assert shader.count('#include "sun_sky_aperture_filter.glsl"')==1
    assert shader.count("sun_sky_aperture_filtered_coverage(")==1

def aperture(sky,receiver):
    return sky<0.999999 and sky+0.00001<receiver

def smoothstep(edge0,edge1,v):
    t=max(0.0,min(1.0,(v-edge0)/(edge1-edge0)))
    return t*t*(3-2*t)

def filtered(sky,receiver,fx,fy):
    # Neighbor taps are [top-left, top-right, bottom-left, bottom-right].
    mask=[1.0 if aperture(z,receiver) else 0.0 for z in sky]
    weights=[(1-fx)*(1-fy),fx*(1-fy),(1-fx)*fy,fx*fy]
    return smoothstep(0.25,1.0,sum(a*w for a,w in zip(mask,weights)))

assert not aperture(1.0,0.9)
assert not aperture(0.8,0.5)
assert aperture(0.1,0.5)
assert not aperture(0.5,0.5)
# Completely enclosed areas stay fully shadowed at all subtexel offsets.
for fx,fy in ((0,0),(0.1,0.7),(0.5,0.5),(0.9,0.25),(1,1)):
    assert filtered([1,1,1,1],0.9,fx,fy)==0.0
    assert filtered([0.1]*4,0.8,fx,fy)==1.0
    assert filtered([0.9]*4,0.8,fx,fy)==0.0
# Along a sky/solid edge, visibility varies continuously, not as a 0/1
# staircase. Below 25% sky coverage the gate remains fully closed.
sky=[0.1,1.0,0.1,1.0]
assert filtered(sky,0.8,0.95,0.5)==0.0
assert filtered(sky,0.8,0.5,0.5)>0.0
assert filtered(sky,0.8,0.5,0.5)<1.0
assert 0.98<filtered(sky,0.8,0.05,0.5)<1.0
for path in ("renderer/renderer.vcxproj","renderer/renderer.vcxproj.filters"):
    assert "sun_sky_aperture_gpu.hpp" in read(path)
print("Opt-in sunlight sky-aperture source contracts passed")

# A sky-blocked receiver should remain unlit by DIRECT sun, but its
# baked brightness is no longer protected from sun shadow contrast.
def shaded_baked(base,intensity,visibility,sky_access,baseline):
    direct_visibility=visibility*sky_access
    contrast=baseline*min(max(intensity,0.0),1.0)
    return base*(1.0-contrast*(1.0-direct_visibility))

for baseline in (0.6,0.75):
    for intensity in (0.0,0.001,0.5,1.0,2.0):
        assert shaded_baked(0.8,intensity,1.0,1.0,baseline)==0.8
        assert abs(shaded_baked(0.8,intensity,1.0,0.0,baseline) -
                   shaded_baked(0.8,intensity,0.0,1.0,baseline))<1e-9
        if intensity==0.0:
            assert shaded_baked(0.8,intensity,1.0,0.0,baseline)==0.8
        if intensity>=1.0:
            assert abs(shaded_baked(0.8,intensity,1.0,0.0,baseline) -
                       0.8*(1.0-baseline))<1e-9

# Keep the depth-only sky atlas point sampled: only the results of the
# individual comparison tests may be interpolated.
gpu_resource=read("renderer/sun_sky_aperture_gpu.hpp")
assert "GL_TEXTURE_2D_ARRAY,GL_TEXTURE_MIN_FILTER,GL_NEAREST" in gpu_resource
assert "GL_TEXTURE_2D_ARRAY,GL_TEXTURE_MAG_FILTER,GL_NEAREST" in gpu_resource
