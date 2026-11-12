"""Source/packaging contracts for the experimental planar capture pass."""
import json
from pathlib import Path
root=Path(__file__).resolve().parents[1]
read=lambda path:(root/path).read_text(encoding="utf-8")
settings=json.loads(read("assets/jkgm.json"))
assert settings["experimental_planar_reflection_capture"] is False
assert settings["planar_reflection_resolution_scale"]==0.5
config=read("common/config.cpp")
for name in ("experimental_planar_reflection_capture",
             "planar_reflection_resolution_scale"):
    assert f'j.contains("{name}")' in config
assert 'std::isfinite(v)' in config and 'v<0.125f || v>1.0f' in config
renderer=read("renderer/renderer.cpp")
target=read("renderer/planar_reflection_capture_target.hpp")
shader=read("renderer/planar_reflection_capture_shader.hpp")
v=read("assets/shaders/planar_capture.vert")
f=read("assets/shaders/planar_capture.frag")
assert "render_planar_capture();" in renderer
assert "select_planar_capture_plane(" in renderer
# Passable adjoins frequently carry water: eligible as reflective RECEIVERS,
# while the mirrored scene itself must still omit passable portal geometry.
selector=renderer.split("void select_planar_capture_plane(",1)[1].split(
    "void render_planar_capture()",1)[0]
assert "surface.count<3U || surface.passable_adjoin" not in selector
assert "if(surface.passable_adjoin)++matched_passable;" in selector
assert "mat.blend==material_blend_mode::fluid ? 1 : 0" in selector
assert "passable_reflection_receivers=" in selector
capture=renderer.split("void render_planar_capture()",1)[1]
assert "face.count==0U || face.passable_adjoin" in capture
assert "make_planar_reflection_camera(" in renderer
assert "make_planar_reflection_render_setup(" in renderer
assert "planar_reflection_gl_scope restore;" in renderer
assert "for(auto const &surface:cache.mesh.surfaces)" in renderer
assert "collect_planar_reflection_things(read,world,scan)" in renderer
assert "acquire_planar_reflection_offscreen_gpu(" in renderer
assert "hidden_by_cur_geomode=" in renderer
main=read("renderer/main.cpp")
assert "thing.thing+0xf8U,&native_geo" in main
assert "native_geo<3" in main
assert "gpu_3do_effective_geo_mode(cur_geo,mesh.geometry_mode)" in main
assert "inst.effective_geometry_mode<3U" in renderer
assert "gpu_3do_native_modelview(" in renderer
assert "planar_capture_target->framebuffer()" in renderer
# World MAT sampling happens AFTER same-frame capture, not from stale history.
shader_h=read("renderer/shaders.hpp")
shader_cpp=read("renderer/shaders.cpp")
world_shader=read("assets/shaders/world_cache.frag")
assert "void set_planar_reflection(bool ready" in shader_h
assert "world_cache_preview_shader::set_planar_reflection(" in shader_cpp
assert "planar_reflection_map" in world_shader
assert "world_planar_reflection(" in world_shader
assert "planar_receiver_plane" in world_shader
assert "sample_color.rgb/=sample_color.a" in world_shader
assert "bind_world_planar_reflection(shader);" in renderer
assert "planar_capture_target->color_texture()" in renderer
assert "planar_capture_camera=reflected;" in renderer
assert "glClearColor(0.0f,0.0f,0.0f,0.0f)" in renderer
assert renderer.index("render_planar_capture();") < renderer.index(
    "draw_game_gbuffer_pass(trimdl, posterize_lighting);")
assert "NOTE=planar_world_material_sampling_no_sky_yet" in renderer
# Visual-parity and performance regressions: reflected world data must be
# staged independently of the primary portal subset, without leaking into
# the primary camera's alpha draw queue.
assert "planar_frame_candidates.clear();" in renderer
assert "append_world_cache_candidate(" in renderer
# Inside draw_world_cache_preview(), the captured camera is named cam.
reflect_stage=renderer.split(
    "// The reflected camera may see faces that JK's primary portal",1)[1].split(
    "if(ordered.empty()",1)[0]
assert "cam.frustum.far_plane>0.1f" in reflect_stage
assert "select_planar_capture_plane(cam.camera);" in reflect_stage
assert "cam.camera,*planar_capture_plane" in reflect_stage
assert "&cam.frustum,&reflection_view" in reflect_stage
assert "camera.camera" not in reflect_stage and "can_cull" not in reflect_stage
assert "stage_candidate(candidate,false)" in renderer
assert "reflection_staged.push_back(candidate)" in renderer
assert "stream_candidates.insert(stream_candidates.end()," in renderer
assert "for(auto const &face:planar_frame_candidates)" in renderer
assert "gpu_3do_batch_outside_frustum(" in renderer
assert "glMultiDrawElements(GL_TRIANGLES,world_counts.data()" in renderer
assert "ms_acquire=" in renderer and "ms_world=" in renderer
assert "ms_models_and_mips=" in renderer
assert "shader.set_color_effects(" in renderer
assert "shader.set_fog(" in renderer
assert "shader.set_model_lighting(" in renderer
assert "world_planar_reflection(nw,roughness)" in world_shader
assert "ray=reflect(incident,normalize(mapped_normal))" in world_shader
assert "textureLod(planar_reflection_map,uv,lod)" in world_shader
assert "uv+=vec2(dot(bump" not in world_shader
assert "glGenerateMipmap(GL_TEXTURE_2D);" in renderer
assert "GL_LINEAR_MIPMAP_LINEAR" in target
assert "glGenerateMipmap(GL_TEXTURE_2D);" in target
assert "uniform vec4 fog_params;" in f
assert "mix(lit,fog_color,fog_far)" in f
# Reflection distance already includes the camera->receiver leg. Its
# contribution must be removed from the capture fog, then reconstructed by
# the primary renderer without counting the first leg twice.
assert "float plane_fraction=clamp(" in f
assert "float fog_total=capture_fog_fraction(total_depth);" in f
assert "float fog_near=capture_fog_fraction(total_depth*plane_fraction);" in f
assert "fog_far=clamp((fog_total-fog_near)/" in f
def fog_at(depth,start,end,opacity):
    return max(0.0,min(1.0,(depth-start)/(end-start)))*opacity
for start,end,opacity,total_depth,fraction in (
    (4.0,40.0,1.0,22.0,0.3),
    (4.0,40.0,0.7,30.0,0.6),
    (0.0,80.0,1.0,80.0,0.25),
    (8.0,32.0,0.6,15.0,0.1),
    (8.0,32.0,0.9,5.0,0.4),
):
    total=fog_at(total_depth,start,end,opacity)
    near=fog_at(total_depth*fraction,start,end,opacity)
    far=max(0.0,min(1.0,(total-near)/max(1.0-near,0.00001)))
    assert abs(near+(1.0-near)*far-total)<1e-6
# Emission is a separate sampled material contribution: it survives a
# black/zero-alpha albedo and retains HDR values after capture.
assert "uniform sampler2D emissive_map;" in f
assert "emitted*=texture(emissive_map,tex_uv+emissive_scroll).rgb;" in f
assert "if(emissive_only!=0)" in f
assert "vec3 lit=albedo.rgb*material_tint.rgb*baked_light+emitted;" in f
assert "shader.set_emission(mat.has_emission,mat.emission_factor," in renderer
assert "glBlendFuncSeparate(GL_ONE,GL_ONE,GL_ONE,GL_ZERO);" in renderer
assert "glDepthMask(GL_FALSE);" in renderer
assert "glDepthFunc(GL_LEQUAL);" in renderer
assert "emissive_world.push_back(" in renderer
assert "emissive_models.push_back(" in renderer
assert "mat.special || mat.emissive_only" not in capture
assert "GL_RGBA16F" in target
# A new sampler must have its GL unit restored for later native/GPU draws.
assert "glActiveTexture(GL_TEXTURE1);" in target
assert "glBindTexture(GL_TEXTURE_2D,GLuint(texture1_));" in target
assert "void set_emission(bool textured" in shader
assert "native_sector_tint" in v and "native_cmp_or_mode" in v
assert "jk_capture_color(native_light,native_cmp_or_mode," in v
assert "clamp(native_light,vec3(0.45)" not in v
assert "instance.ambient=1.0f;" not in main


assert "GL_COLOR_ATTACHMENT0" in target
assert "GL_DEPTH_COMPONENT24" in target
assert "glGetIntegerv(GL_DRAW_FRAMEBUFFER_BINDING" in target
assert "glBindFramebuffer(GL_DRAW_FRAMEBUFFER" in target
assert "clip_plane_view" in v and "if(clip_side<" in f
assert "is_model" in v and "native_uv_offset" in v
assert "albedo_map" in f and "alpha_cutoff" in f
for p in ("renderer/renderer.vcxproj","renderer/renderer.vcxproj.filters"):
    s=read(p)
    for name in ("planar_reflection_capture_target.hpp",
                 "planar_reflection_capture_shader.hpp",
                 "planar_reflection_render_math.hpp"):
        assert name in s
assert 'Copy-Item -Path \'assets/shaders/*\'' in read(".github/workflows/main.yml")
print("Configurable planar FBO/camera/mirror capture source contracts OK")
