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
for name in ("sun_shadow_vsm_near_radius","sun_shadow_vsm_far_radius",
             "sun_shadow_vsm_min_stddev_world",
             "sun_shadow_vsm_bleed_reduction"):
    assert f'parse_vsm_radius("{name}"' in parser
    assert f"**{name}**" in read("doc/config.md")
assert "std::isfinite(radius)" in parser
assert "float sun_shadow_vsm_min_stddev_world = 0.01f;" in cfg
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
    assert "min_stddev*min_stddev" in src
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
print("Experimental sun VSM filter source contracts passed")

# Same physical VSM distribution under two ortho depth ranges must yield
# the same probability once the variance floor is expressed in world units.
def chebyshev(range_jku, receiver_jku, mean_jku, variance_jku2,
              stddev_floor_jku=0.01, bleed=0.2):
    m=mean_jku/range_jku
    v=max(variance_jku2/(range_jku**2),
          (stddev_floor_jku/range_jku)**2)
    r=receiver_jku/range_jku
    if r<=m:return 1.0
    p=v/(v+(r-m)**2)
    return max(0.0,min(1.0,(p-bleed)/(1.0-bleed)))
for receiver in (0.05,0.1,0.2,0.5):
    assert abs(chebyshev(10.0,receiver,0.03,0.0001) -
               chebyshev(250.0,receiver,0.03,0.0001))<1e-10
assert chebyshev(10,0.1,0.03,0.0001,bleed=0.5) <= chebyshev(
    10,0.1,0.03,0.0001,bleed=0.2)
