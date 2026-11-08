"""Opt-in cached VSM prefilter for near and far sun shadows."""
from pathlib import Path
root=Path(__file__).resolve().parents[1]
def read(p):return (root/p).read_text(encoding="utf-8")
cfg,parser=read("common/config.hpp"),read("common/config.cpp")
prog,progcpp=read("renderer/shaders.hpp"),read("renderer/shaders.cpp")
state,statecpp=read("renderer/opengl_state.hpp"),read("renderer/opengl_state.cpp")
renderer=read("renderer/renderer.cpp")
storage=read("renderer/sun_shadow_moments_gpu.hpp")
flt=read("assets/shaders/sun_shadow_prefilter.frag")
opaque=read("assets/shaders/post_opaque_composite.frag")
forward=read("assets/shaders/translucent_sun_sampling.glsl")
assert "bool experimental_sun_shadow_vsm = false;" in cfg
assert 'j.contains("experimental_sun_shadow_vsm")' in parser
for name in ("sun_shadow_vsm_near_radius","sun_shadow_vsm_far_radius"):
    assert f'parse_vsm_radius("{name}"' in parser
    assert f"**{name}**" in read("doc/config.md")
for name in ("sun_shadow_vsm_min_stddev_world",
             "sun_shadow_vsm_bleed_reduction"):
    assert f'parse_vsm_scalar("{name}"' in parser
    assert f"**{name}**" in read("doc/config.md")
assert "std::isfinite(radius)" in parser
assert "float sun_shadow_vsm_min_stddev_world = 0.0f;" in cfg
assert "float sun_shadow_vsm_bleed_reduction = 0.2f;" in cfg
assert 'parse_vsm_scalar("sun_shadow_vsm_min_stddev_world"' in parser
assert 'parse_vsm_scalar("sun_shadow_vsm_bleed_reduction"' in parser
assert "std::isfinite(v)" in parser
assert "sun_shadow_prefilter_shader" in prog and "sun_shadow_prefilter_shader" in progcpp
assert "if(prefiltered_sun && type==gl::shader_type::fragment)" in progcpp
assert "#define JKGM_SUN_VSM 1" in progcpp
assert "prefiltered_sun=false" in prog
assert "sun_shadow_prefilter_program" in state
assert "experimental_sun_shadow_vsm &&" in statecpp
assert "std::unique_ptr<sun_shadow_moments_gpu> sun_moments_target;" in renderer
assert "sun_moments_target.reset();" in renderer
assert "sun_moments_target->valid[cascade]" in renderer
assert "if(unchanged) {" in renderer
assert "regenerate_sun_shadow_moments(cascade," in renderer
assert "sun_shadow_ready=sun_shadow_ready && sun_moments_target &&" in renderer
assert "gl::texture_compare_mode::none" in renderer
assert "gl::texture_internal_format::rg32f" in storage
assert "std::array<bool,2> valid" in storage
assert "uniform sampler2D source_image;" in flt
assert "vec2(d,d*d)" in flt and "axis==0" in flt
assert "out_moments=sum;" in flt
for src in [opaque,forward]:
    assert "#ifdef JKGM_SUN_VSM" in src
    assert "moments.y-moments.x*moments.x" in src
    assert "max(2.0e-7,min_stddev*min_stddev)" in src
    assert "return clamp((probability-bleed)/(1.0-bleed),0.0,1.0);" in src
    assert "sampler2DShadow" in src and "sampler2D map" in src
assert "min_stddev=sun_vsm_params.x/max(range,0.001)" in opaque
assert "min_stddev=translucent_sun_vsm_params.x*inv_range" in forward
assert "float inv_range=length(translucent_sun_map_z[slot].xyz);" in forward
assert "set_sun_vsm_params" in prog and "set_sun_vsm_params" in progcpp
assert "set_vsm_params" in prog and "set_vsm_params" in progcpp
assert "uniforms.set_vsm_params(" in renderer
assert "prog.set_sun_vsm_params(" in renderer
for name in ("game_opaque_pass.frag","game_trns_pass.frag","gpu_3do_preview.frag"):
    s=read("assets/shaders/"+name)
    assert "uniform sampler2D translucent_sun_near_depth;" in s
    assert "uniform sampler2DShadow translucent_sun_near_depth;" in s
    assert "uniform vec2 translucent_sun_vsm_params;" in s
    assert "uniform sampler2D translucent_sun_far_depth;" in s
    assert "uniform sampler2DShadow translucent_sun_far_depth;" in s
assert "sun_dynamic_image;" in opaque
assert "translucent_sun_dynamic_depth;" in read("assets/shaders/game_trns_pass.frag")
assert "spotlight_shadow_image;" in opaque
# The floating-point precision guard must remain at least 2e-7
# even when the optional world-space floor is zero or the depth
# projection changes. Zero must reproduce the ORIGINAL VSM behavior.
def normalized_variance(depth_range,raw_variance,world_stddev=0.0):
    return max(raw_variance,2.0e-7,(world_stddev/depth_range)**2)
for depth_range in (1.0,20.0,100.0,500.0):
    for raw in (-1e-5,-1e-9,0.0,1e-9,1e-7,3e-7):
        assert normalized_variance(depth_range,raw,0.0)==max(raw,2e-7)
        assert normalized_variance(depth_range,raw,0.01)>=max(raw,2e-7)
assert normalized_variance(1,0.0,0.1)>normalized_variance(1,0.0,0.0)

def chebyshev(depth_range,receiver,mean,raw_variance,
              stddev_floor=0.0,bleed=0.2):
    m=mean/depth_range
    r=receiver/depth_range
    if r<=m:return 1.0
    var=normalized_variance(depth_range,raw_variance,stddev_floor)
    p=var/(var+(r-m)**2)
    return max(0.0,min(1.0,(p-bleed)/(1.0-bleed)))
for receiver in (0.05,0.1,0.2,0.5):
    p=chebyshev(100.0,receiver,0.03,-1.0e-8)
    assert 0.0<=p<=1.0
assert chebyshev(10.0,0.1,0.03,0.0001,bleed=0.5) <= chebyshev(
    10.0,0.1,0.03,0.0001,bleed=0.2)

print("Experimental sun VSM filter source contracts passed")
