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
# GPU world uses the same VSM moments as forward 3DO receivers. Its linked
# shader must be compiled for the matching sampler type and compare formula.
world=read("assets/shaders/world_cache_direct_specular.glsl")
state_programs=read("renderer/opengl_state.cpp")
assert "world_cache_preview_shader(fs::path const &data_root,\n                                                bool prefiltered_sun=false);" in prog
assert "world_cache_preview_shader::world_cache_preview_shader(\n    fs::path const &root,bool prefiltered_sun)" in progcpp
assert 'root/"shaders/world_cache.frag",prefiltered_sun)' in progcpp
assert "std::make_unique<shaders::world_cache_preview_shader>(\n                data_root,the_config->experimental_sun_shadow_vsm &&\n                          the_config->enable_sun_shadows)" in state_programs
assert "#ifdef JKGM_SUN_VSM" in world
assert "uniform sampler2D translucent_sun_near_depth;" in world
assert "uniform sampler2D translucent_sun_far_depth;" in world
assert "uniform sampler2DShadow translucent_sun_near_depth;" in world
assert "uniform sampler2DShadow translucent_sun_far_depth;" in world
assert "uniform sampler2DShadow translucent_sun_dynamic_depth;" in world
assert "uniform vec2 translucent_sun_vsm_params;" in world
assert "world_sun_cascade_filter(translucent_sun_near_depth," in world
assert "world_sun_cascade_filter(translucent_sun_far_depth," in world
assert "moments.y-moments.x*moments.x" in world
assert "max(2.0e-7,min_stddev*min_stddev)" in world
assert "return clamp((probability-bleed)/(1.0-bleed),0.0,1.0);" in world
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

# The debug visualization was removed. The raw-depth samplers remain
# solely for the optional VSM depth guard, never for regular PCF shadows.
assert "sun_shadow_debug_view" not in cfg
assert "sun_shadow_debug_view" not in parser
assert "sun_shadow_debug_view" not in prog
assert "sun_shadow_debug_view" not in progcpp
assert "sun_shadow_debug_view" not in renderer
assert "sun_shadow_debug_view" not in opaque
assert "uniform sampler2D sun_shadow_raw_near;" in opaque
assert "uniform sampler2D sun_shadow_raw_far;" in opaque
assert "uniform sampler2DShadow sun_shadow_raw_near;" not in opaque
assert "uniform sampler2DShadow sun_shadow_raw_far;" not in opaque
assert "sun_shadow_visibility(world_position,incidence)" in opaque
assert "sun_sky_aperture(world_position)" in opaque

# Optional raw-depth cap suppresses false VSM illumination in opaque
# receivers. No changes to depth maps, VSM moment filters or PCF shaders.
assert "bool experimental_sun_vsm_depth_guard = false;" in cfg
assert 'j.contains("experimental_sun_vsm_depth_guard")' in parser
assert "bool const vsm_depth_guard=sun_sky_aperture_hardware_supported() &&" in renderer
assert "the_config->experimental_sun_vsm_depth_guard &&" in renderer
assert "sun_shadow_ready && bool(sun_shadow_target);" in renderer
assert "prog.set_sun_vsm_depth_guard(vsm_depth_guard);" in renderer
assert "if(vsm_depth_guard) {" in renderer
assert "gl::set_uniform_integer(sun_shadow_raw_near,17);" in progcpp
assert "gl::set_uniform_integer(sun_shadow_raw_far,18);" in progcpp
assert "void set_sun_vsm_depth_guard(bool enabled);" in prog
assert "game_post_opaque_composite_shader::set_sun_vsm_depth_guard(" in progcpp
assert "uniform int sun_vsm_depth_guard;" in opaque
assert "sun_shadow_active!=0 &&" in opaque
assert "sun_vsm_depth_guard!=0 && visibility>0.0)" in opaque
assert "sun_vsm_raw_depth_visibility(" in opaque
assert "texelFetch(map,tap,0).r" in opaque
assert "#if defined(JKGM_SUN_VSM) && defined(JKGM_SUN_SKY_APERTURE)" in opaque
# Fully shadowed raw depths must reject VSM bright artifacts, but the
# pixel-edge case remains fractional, rather than an abrupt 0/1 boundary.
def raw_cap(vsm,taps,u,v):
    weights=[(1-u)*(1-v),u*(1-v),(1-u)*v,u*v]
    return min(vsm,sum(x*w for x,w in zip(taps,weights)))
assert raw_cap(1.0,[0,0,0,0],0.4,0.3)==0.0
assert raw_cap(0.7,[1,1,1,1],0.4,0.3)==0.7
assert 0<raw_cap(1.0,[0,1,0,1],0.4,0.3)<1
