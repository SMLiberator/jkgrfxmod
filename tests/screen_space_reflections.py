"""Source-contract checks for the first low-resolution GPU-world SSR pass."""
import json
from pathlib import Path

root=Path(__file__).resolve().parents[1]
read=lambda path:(root/path).read_text(encoding="utf-8")
settings=json.loads(read("assets/jkgm.json"))
assert settings["experimental_screen_space_reflections"] is False
assert settings["ssr_resolution_scale"]==0.125
assert settings["ssr_ray_steps"]==32
assert settings["ssr_ray_thickness"]>0
assert settings["ssr_max_distance"]>0
config=read("common/config.cpp")
for name in ("experimental_screen_space_reflections","ssr_resolution_scale",
             "ssr_ray_steps","ssr_ray_thickness","ssr_max_distance"):
    assert f'j.contains("{name}")' in config
renderer=read("renderer/renderer.cpp")
ssr=read("renderer/screen_space_reflections_renderer.inc")
h=read("renderer/screen_space_reflections.hpp")
receive_v=read("assets/shaders/ssr_world_receiver.vert")
receive_f=read("assets/shaders/ssr_world_receiver.frag")
trace=read("assets/shaders/ssr_trace.frag")
world=read("assets/shaders/world_cache.frag")
units=read("renderer/texture_units.hpp")
# An ordinary shader/material mode=1 opts in. No implicit Fluid fallback.
assert "if(mat.reflection_mode!=1)continue;" in ssr
assert "choose_world_cache_draw_policy(face,info)" in ssr
assert "world_cache_pass::transparent" in ssr
assert "mat.blend==material_blend_mode::fluid" not in ssr
# Capture runs AFTER opaque composition, BEFORE forward translucent draws.
gbuf=renderer.split("void draw_game_gbuffer_pass(",1)[1].split(
    "// One MAT binder shared by opaque",1)[0]
assert gbuf.index("frame_screen->end_compose_opaque_pass();") < gbuf.index(
    "render_world_screen_reflections();")
render_game=renderer.split("void present_game()",1)[1]
assert render_game.index("draw_game_gbuffer_pass(") < render_game.index(
    "draw_game_transparency_pass(")
assert '#include "screen_space_reflections_renderer.inc"' in renderer
assert '#include "screen_space_reflections.hpp"' in renderer
assert "get_opaque_composite_texture()" in ssr
# MSAA must expose live single-sample composited color, not stale end_frame resolve.
for path in ("renderer/renderer_screen_basic.cpp","renderer/renderer_screen_msaa.cpp"):
    assert "get_opaque_composite_texture()" in read(path)
    assert "return screen_buffer.tex;" in read(path)
# Two offscreen FBOs: never read an image attached for drawing.
assert "ssr_receivers->framebuffer()" in ssr
assert "ssr_image->framebuffer()" in ssr
assert "ssr_receivers->color_texture()" in ssr
assert "frame_screen->get_opaque_composite_texture()" in ssr
assert "offscreen_capture_gl_scope restore;" in ssr
assert "previous_unit2" in ssr
assert "ssr_resolution_scale" in ssr
assert "ssr_ray_steps" in ssr
assert "ssr_ray_thickness" in ssr
assert "ssr_max_distance" in ssr
assert "ssr_ready=true;" in ssr
assert "ssr_receiver_program" in ssr and "ssr_trace_program" in ssr
# The receiver stage is actual world geometry at reduced resolution, with
# per-material normal maps and opaque scene depth rejection.
assert "layout(location=0) in vec3 world_position;" in receive_v
assert "native_uv_offset" in receive_v
assert "gl_FragCoord.w" in receive_f
assert "if(depth>opaque+" in receive_f
assert "normal_map2" in receive_f and "dFdx" in receive_f
assert "out_normal_depth=vec4(n,depth);" in receive_f
assert "texelFetch(receiver_normal_depth,ivec2(gl_FragCoord.xy),0)" in trace
# Bounded true SSR raymarch, refinement, edge fade, zero-coverage fallback.
assert "for(int i=1;i<=96;++i)" in trace
assert "if(i>steps)break;" in trace
assert "for(int j=0;j<8;++j)" in trace
assert "float final_delta=delta;" in trace
assert "if(final_delta<=thickness)" in trace
assert "smoothstep(0.0,0.035,edge)" in trace
assert "out_reflection=vec4(0.0);" in trace
assert "texture(opaque_color,found).rgb*confidence" in trace
assert "glGenerateMipmap(GL_TEXTURE_2D);" in ssr
assert "textureLod(screen_reflection_map," in world
assert "screen.rgb/=screen.a;" in world
# Only the material receiving SSR combines it with an EnvMap; no forced
# world-only reflection fallback and no bounce feedback during opaque draw.
assert "bool has_screen=mode==1 && screen_reflection_ready!=0;" in world
assert "env=mix(env,screen.rgb,clamp(screen.a,0.0,1.0));" in world
assert "shader.set_screen_reflection(false,9);" in renderer
assert "shader.set_screen_reflection(ssr_ready,9);" in renderer
assert "if(has_fluid || ssr_ready)" in renderer
assert '{"screen_reflection_map",9,sampler_kind::tex2d}' in units
# Opaque world and cached model groups reuse the same screen-mode opt-in.
opaque=read("renderer/screen_space_reflections_opaque.inc")
receiver_opaque=read("assets/shaders/ssr_opaque_receiver.frag")
model_vert=read("assets/shaders/ssr_opaque_model.vert")
compositor=read("assets/shaders/ssr_opaque_composite.frag")
assert '#include "screen_space_reflections_opaque.inc"' in renderer
assert "render_opaque_screen_reflections();" in gbuf
assert "if(mat.reflection_mode!=1 ||" in opaque
assert "world_cache_pass::opaque" in opaque
assert "gpu_3do_draws_in_pass(policy,false)" in opaque
assert "gpu_3do_native_modelview(" in opaque
assert "packed_vertex_format?1:0" in opaque
assert "ssr_opaque_receivers->framebuffer()" in opaque
assert "ssr_opaque_image->framebuffer()" in opaque
assert "frame_screen->begin_compose_opaque_pass(true);" in opaque
assert "glBlendFuncSeparate(GL_SRC_ALPHA,GL_ONE_MINUS_SRC_ALPHA," in opaque
assert "GL_ZERO,GL_ONE);" in opaque
assert "gl::texture_view(ssr_opaque_image->color_texture())" in opaque
assert "gl::texture_view(ssr_opaque_receivers->color_texture())" in opaque
assert "material_params.z" in receiver_opaque and "mode==2" in receiver_opaque
assert "abs(linear_depth-scene_depth)" in receiver_opaque
assert "packed_vertex_format" in model_vert
assert "decode_oct(receiver.xy)" in trace
assert "if(opaque_receiver_mode!=0)" in trace
assert "receiver.z" in compositor
assert "glGenerateMipmap(GL_TEXTURE_2D);" in opaque
assert "render_opaque_screen_reflections();" in renderer
for p in ("renderer/renderer.vcxproj","renderer/renderer.vcxproj.filters"):
    assert "screen_space_reflections.hpp" in read(p)
    assert "screen_space_reflections_renderer.inc" not in read(p)
assert "tests/screen_space_reflections.py" in read(".github/workflows/main.yml")
print("Low-resolution cached-world SSR source contracts OK")
