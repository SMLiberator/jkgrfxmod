"""Fixed dual-paraboloid hemi and six-face cube source contracts."""
import json
from pathlib import Path
root=Path(__file__).resolve().parents[1]
read=lambda p:(root/p).read_text(encoding="utf-8")
settings=json.loads(read("assets/jkgm.json"))
assert settings["experimental_hemi_reflection_capture"] is False
assert settings["reflection_capture_resolution"]==128
assert settings["experimental_cube_reflection_capture"] is False
assert settings["reflection_capture_update_interval"]==0.0
mat=read("common/material.hpp")
parser=read("common/material_map.cpp")
config=read("common/config.cpp")
renderer=read("renderer/renderer.cpp")
capture=read("renderer/hemi_reflection_capture.inc")
shader=read("assets/shaders/gpu_3do_preview.frag")
units=read("renderer/texture_units.hpp")
assert "matcap, planar, hemi, cube" in mat
assert 'v == "hemi"' in parser
assert 'v == "cube"' in parser
assert "material_reflection_mode::cube" in parser
assert "material_reflection_mode::hemi" in parser
assert "reflection_capture_resolution" in config
assert "experimental_cube_reflection_capture" in config
assert "reflection_capture_update_interval" in config
assert "reflection_probe_max_owners" in config
assert "reflection_capture_update_interval" in config
assert settings["reflection_probe_max_owners"] == 4
assert settings["reflection_capture_update_interval"] == 0.0
assert '#include "hemi_reflection_camera.hpp"' in renderer
assert '#include "hemi_reflection_capture.inc"' in renderer
assert "render_hemi_capture();" in renderer
assert "std::unordered_set<uint64_t> acquired;" in capture
assert "planar_reflection_thing_owner_key(thing)" in capture
assert "acquire_planar_reflection_offscreen_gpu(" in capture
assert "for(auto const &surface:cache.mesh.surfaces)" in capture
assert "mode!=renderer_mode::ingame" in capture
assert "for(int face=0;face<face_count;++face)" in capture
assert "std::sort(candidates.begin(),candidates.end()" in capture
assert "candidates.resize(the_config->reflection_probe_max_owners)" in capture
assert "std::chrono::duration<float>(" in capture
assert "entry.seen_frame=gpu_3do_present_serial" in capture
assert "entry.last_refresh=now;" in capture
assert "probe_target->select_layer(face)" in capture
assert "reflection_probes.find(inst.shadow_owner)" in renderer
assert "probe->target->color_texture()" in renderer
assert "owner_probe->second.ready" in renderer
assert "owner_probe->second.mode" in renderer
assert "textureLod(hemi_reflection_map," in shader
assert "make_cube_reflection_camera(" in capture
assert "draw_reflection_capture_sky(shader,camera,view,frustum," in capture
assert "the_config->reflection_capture_resolution;" in capture
assert "the_config->reflection_capture_update_interval" in capture
assert "glGenerateMipmap(GL_TEXTURE_2D_ARRAY)" in capture
assert "reflection_probe_capture_target" in read("renderer/offscreen_capture_target.hpp")
assert "(mode==5 || mode==6) && hemi_capture_info.z>0.5" in shader
assert "sampler2DArray hemi_reflection_map" in shader
assert "layer=5" in shader
assert "textureLod(hemi_reflection_map," in shader
assert "env=mix(env,capture.rgb" in shader
assert "roughness*log2" in shader and "fresnel" in shader
assert "float coverage=textureLod(hemi_reflection_map," in shader
assert "clamp(coverage*softness" in shader
assert "capture.rgb/=capture.a" in shader
assert '{"hemi_reflection_map",23,sampler_kind::texture_array}' in units
assert "experimental_screen_space_reflections" in read("common/config.hpp")
assert "gpu_3do_preview" in units
assert "hemi_reflection_camera.hpp" in read("renderer/renderer.vcxproj")
assert "hemi_reflection_camera.hpp" in read("renderer/renderer.vcxproj.filters")
assert "hemi_reflection_capture.inc" not in read("renderer/renderer.vcxproj")
assert "render_hemi_capture();" in renderer
assert "JKGM_REFLECTION_PROBE 1" in read("renderer/shaders.cpp")
print("HEMI and CUBE material, capture, sampling, and fallback contracts OK")

# Hemi uses a reliable cube capture as a temporary conversion source.
# Sampling itself is now fixed-axis up/down, independent of player view.
assert '#include "dual_paraboloid_converter.hpp"' in renderer
assert "std::unique_ptr<reflection_probe_capture_target> cube_source;" in renderer
assert "dual_paraboloid_convert_program->convert(" in capture
assert "int const output_layers=paraboloid_mode?2:6;" in capture
assert "int const face_count=direct_paraboloid?2:6;" in capture
assert "make_hemi_reflection_camera(" not in capture
assert "1.091308501" in shader
assert "up_weight=smoothstep(" in shader
assert "for(int hemisphere=0;hemisphere<2;++hemisphere)" in shader
assert "weighted_coverage" in shader
assert "dual_paraboloid_projection.hpp" in read("renderer/dual_paraboloid_converter.hpp")
assert "dual_paraboloid_convert_program.reset();" in renderer

# Direct capture is an opt-in two-view path; CUBE and global remain six-view.
assert settings["experimental_hemi_direct_capture"] is False
assert "experimental_hemi_direct_capture" in config
assert "make_dual_paraboloid_camera(focus,unsigned(face))" in capture
assert "direct_paraboloid?2:6" in capture
assert "shader.set_paraboloid(unsigned(face),far_plane)" in capture
assert "direct_paraboloid?nullptr:&frustum" in capture
assert "direct_paraboloid?nullptr:&view" in capture
assert "dual_paraboloid_outside_distance(" in capture
assert "glEnable(GL_CLIP_DISTANCE0)" in capture
assert "glEnable(GL_CLIP_DISTANCE1)" in capture
assert "glEnable(GL_CLIP_DISTANCE2)" in capture
assert "paraboloid_capture_program" in renderer
assert "GL_CLIP_DISTANCE0" in read("renderer/offscreen_capture_target.hpp")
assert "dual_paraboloid_capture.geom" in read("renderer/planar_reflection_capture_shader.hpp")
assert "gl_ClipDistance[0]" in read("assets/shaders/dual_paraboloid_capture.geom")
assert "gl_FragDepth" in read("assets/shaders/dual_paraboloid_capture.frag")
assert "paraboloid" in read("renderer/reflection_sky_capture.inc")
