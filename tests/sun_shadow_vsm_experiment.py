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
assert "std::isfinite(radius)" in parser
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
    assert "return clamp((probability-0.2)/0.8,0.0,1.0);" in src
    assert "sampler2DShadow" in src and "sampler2D map" in src
for name in ("game_opaque_pass.frag","game_trns_pass.frag","gpu_3do_preview.frag"):
    s=read("assets/shaders/"+name)
    assert "uniform sampler2D translucent_sun_near_depth;" in s
    assert "uniform sampler2DShadow translucent_sun_near_depth;" in s
    assert "uniform sampler2D translucent_sun_far_depth;" in s
    assert "uniform sampler2DShadow translucent_sun_far_depth;" in s
assert "sun_dynamic_image;" in opaque
assert "translucent_sun_dynamic_depth;" in read("assets/shaders/game_trns_pass.frag")
assert "spotlight_shadow_image;" in opaque
print("Experimental sun VSM filter source contracts passed")
