"""Standard quarter-pixel sun visibility; preserve aperture and full-res fallback."""
from pathlib import Path
r=Path(__file__).resolve().parents[1]
read=lambda p:(r/p).read_text(encoding="utf-8")
cfg=read("common/config.hpp")
parser=read("common/config.cpp")
renderer=read("renderer/renderer.cpp")
opaque=read("assets/shaders/post_opaque_composite.frag")
half=read("assets/shaders/post_sun_visibility_half.frag")
units=read("renderer/texture_units.hpp")
state=read("renderer/opengl_state.cpp")
assert "experimental_half_res_sun_visibility" not in cfg
assert "experimental_half_res_sun_visibility" not in parser
assert "experimental_half_res_sun_visibility" not in read("assets/jkgm.json")
assert "experimental_half_res_sun_visibility" not in read("doc/config.md")
assert "if(the_config->enable_sun && the_config->enable_sun_shadows)" in state
assert "game_post_opaque_half_sun_program" in state
assert "sun_half_visibility=std::make_unique<post_buffer>(dims);" in state
assert "glIsEnabled(GL_SCISSOR_TEST)==GL_FALSE" in renderer
assert "glIsEnabled(GL_BLEND)==GL_FALSE" in renderer
assert "half_sun_shader->set_sun_dynamic_count(0);" in renderer
assert "frame_screen->begin_compose_opaque_pass(true);" in renderer
assert "prog.set_sun_half_visibility(half_sun_active,11);" in renderer
assert "sun_half_visibility_image" in units
assert 'post_opaque_half_sun[]=' in units
assert 'bind(texture_units::post_opaque_half_sun);' in read("renderer/shaders.cpp")
assert "sun_half_reconstruct_shadow(tc,depth,vn)" in opaque
assert "if(visibility<0.0)" in opaque
assert "float sky_access=sun_sky_aperture(world_position);" in opaque
assert "sun_shadow_visibility(world_position,incidence)" in opaque
assert "sun_dynamic_shadow_visibility(world_position,incidence)" in opaque
assert "float depth_tolerance=max(0.125,depth*0.002);" in opaque
assert "dot(N,sun_half_decode_oct(half_sample.ba))<0.96" in opaque
assert "bool sprite=chosen_normal.a>1.5;" in half
assert "float incidence=sprite" in half
assert "vec2 sign_nonzero=vec2(e.x>=0.0?1.0:-1.0," in half
assert "sun_dynamic_shadow_visibility(world,incidence)" in half
assert "out_color=vec4(visibility,d,encode_oct(view_n));" in half
assert "sun_sky_aperture(" not in half  # full-resolution hard sky boundary
# Keep identical shader shadow bias/cascade/atlas formulas while duplicated.
def extract(source,begin,end):
    return source.split(begin,1)[1].split(end,1)[0]
start="vec3 sun_shadow_coordinates("
end="// Separate sky aperture from normal occluder"
dynamic="float sun_dynamic_shadow_visibility("
end_dynamic="float spotlight_shadow_visibility("
assert extract(half,start,dynamic).strip()==extract(opaque,start,end).strip()
# The full-res compositor inserts its optional Fluid-caustics block AFTER
# the shared dynamic shadow helpers. Compare only the duplicated shadow code,
# then ensure caustics remain out of the half-resolution visibility pass.
caustics_marker="// Optional Fluid transmission, using the same near/far SUN projections."
assert opaque.count(caustics_marker)==1
assert extract(half,dynamic,"vec2 encode_oct(").strip()==extract(opaque,dynamic,caustics_marker).strip()
assert "sun_caustics_depth" in extract(opaque,caustics_marker,"// Only the costly shadow-map lookups")
assert "sun_caustics_depth" not in half
assert "sample_sun_caustic_light(" not in half
print("Half-resolution opaque sun visibility and full-res sky contracts passed")
