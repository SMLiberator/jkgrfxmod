"""Fixed dual-paraboloid hemi and six-face cube source contracts."""
import json
from pathlib import Path
root=Path(__file__).resolve().parents[1]
read=lambda p:(root/p).read_text(encoding="utf-8")
settings=json.loads(read("assets/jkgm.json"))
assert settings["enable_reflection_capture"] is True
assert settings["enable_reflection_probes"] is False
assert settings["reflection_capture_resolution"]==128
assert settings["reflection_capture_update_interval"]==0.0
mat=read("common/material.hpp")
parser=read("common/material_map.cpp")
config=read("common/config.cpp")
renderer=read("renderer/renderer.cpp")
capture=read("renderer/hemi_reflection_capture.inc")
shader=read("assets/shaders/gpu_3do_preview.frag")
units=read("renderer/texture_units.hpp")
assert "planar=4, hemi=5, cube=6, shared=7" in mat
assert 'v == "hemi"' in parser
assert 'v == "cube"' in parser
assert "material_reflection_mode::cube" in parser
assert "material_reflection_mode::hemi" in parser
assert "reflection_capture_resolution" in config
assert "enable_reflection_capture" in config
assert "enable_reflection_probes" in config
assert "reflection_capture_update_interval" in config
assert "reflection_probe_max_owners" in config
assert 'j.contains("reflection_probe_max_distance")' in config
assert "reflection_probe_max_distance must be 0 (unlimited) or between 4 and 4096" in config
assert "reflection_capture_update_interval" in config
assert settings["reflection_probe_max_owners"] == 4
assert settings["reflection_probe_max_distance"] == 64.0
assert settings["reflection_capture_update_interval"] == 0.0
assert '#include "hemi_reflection_camera.hpp"' in renderer
assert '#include "reflection_attachment_family.hpp"' in renderer
assert 'build_reflection_attachment_family(' in capture
assert 'reflection_excluded_owners.count(owner)' in capture
assert 'reflection_excluded_owners.count(inst.shadow_owner)' in capture
assert 'if(!candidate.is_global && !candidate.is_shared &&' in capture
family=read("renderer/reflection_attachment_family.hpp")
assert 'native_attach_has_thing_parent(attach_flags)' in family
assert '(native_attach_no_move|native_attach_node)' in family
assert 'thing+0x58U,parent' in family
assert 'node.thing+0x68U,child' in family
assert 'at+0x6cU,next' in family
assert 'reflection_attachment_family.hpp' in read("renderer/renderer.vcxproj")
assert 'reflection_attachment_family.hpp' in read("renderer/renderer.vcxproj.filters")
assert '#include "hemi_reflection_capture.inc"' in renderer
assert "render_hemi_capture();" in renderer
assert "std::unordered_set<uint64_t> acquired;" in capture
assert "planar_reflection_thing_owner_key(thing)" in capture
assert "acquire_planar_reflection_offscreen_gpu(" in capture
assert "for(auto const &bucket:reflection_world_buckets.buckets())" in capture
assert "mode!=renderer_mode::ingame" in capture
assert "for(int face=0;face<face_count;++face)" in capture
assert "std::sort(candidates.begin(),candidates.end()" in capture
assert "candidates.resize(the_config->reflection_probe_max_owners)" in capture
assert "std::chrono::duration<float>(" in capture
assert "entry.seen_frame=gpu_3do_present_serial" in capture
assert "entry.last_refresh=now;" in capture
assert "probe_target->select_layer(face)" in capture
assert "reflection_probes.find(inst.shadow_owner)" in renderer
assert "active_probe->target->color_texture()" in renderer
assert "probe_it->second.ready" in renderer
assert "probe_it->second.mode" in renderer
assert "textureLod(hemi_reflection_map," in shader
assert "make_cube_reflection_camera(" in capture
assert "draw_reflection_capture_sky(shader,camera,view,frustum," in capture
assert "the_config->reflection_capture_resolution;" in capture
assert "the_config->reflection_capture_update_interval" in capture
assert "build_capture_portal_view(" in capture
assert "portals.traverse(" in capture
assert "portal_views_used" in capture
assert "surface_mask" in capture
assert "thing_sector_mask" in capture
assert "all_directions_visibility" in capture
assert "cube_visibility[size_t(face)]" in capture
assert "read,world,scan,sector_mask" in capture
assert "glGenerateMipmap(GL_TEXTURE_2D_ARRAY)" in capture
assert "reflection_probe_capture_target" in read("renderer/offscreen_capture_target.hpp")
assert "(mode==5 || mode==6 || mode==7) && hemi_capture_info.z>0.5" in shader
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

# Hemi always uses direct fixed-axis up/down paraboloid capture.
assert '#include "dual_paraboloid_converter.hpp"' not in renderer
assert "cube_source" not in renderer
assert "dual_paraboloid_convert_program" not in capture
assert "int const output_layers=paraboloid_mode?2:6;" in capture
assert "int const face_count=paraboloid_mode?2:6;" in capture
assert "make_hemi_reflection_camera(" not in capture
assert "1.091308501" in shader
assert "up_weight=smoothstep(" in shader
assert "for(int hemisphere=0;hemisphere<2;++hemisphere)" in shader
assert "weighted_coverage" in shader

# Direct hemi capture is permanent; cube/shared and COG remain six-view.
assert "experimental_hemi_direct_capture" not in config
assert "experimental_hemi_direct_capture" not in capture
assert "make_dual_paraboloid_camera(focus,unsigned(face))" in capture
assert "paraboloid_mode?2:6" in capture
assert "shader.set_paraboloid(unsigned(face),far_plane)" in capture
assert "std::array<capture_portal_view,6> cube_visibility{};" in capture
assert "all_directions_visibility=face_visibility" in capture
assert "all_directions_visibility.thing_sectors[i] |=" in capture
assert "if(!complete)all_directions_visibility={};" in capture
assert "cube_visibility[size_t(face)]" in capture
assert "capture_offscreen=\n                    prepare_scene(capture_thing_mask)" in capture
assert "auto const &offscreen=capture_offscreen;" in capture
assert "face_offscreen=prepare_scene(" not in capture
assert "planar_reflection_thing_outside_probe_range(" in capture
assert "if(probe_distance_limit>0.0f &&" in capture
assert "reflection_probe_capture_distance_limit(" in capture
assert "frustum.clip_far=0;" in capture
assert "probe_distance_limit)) {" in capture
assert "reflection_probe_bounds_outside_range(" in capture
assert "float const far_plane=reflection_capture_scene_far(focus);" in capture
# The Thing radius must never enter world-surface culling or projection.
world_capture=capture.split(
    "for(auto const &bucket:reflection_world_buckets.buckets())",1)[1].split(
    "auto draw_model=[&]",1)[0]
assert "probe_distance_limit" not in world_capture
assert "reflection_probe_bounds_outside_range(" not in world_capture
assert "view,surface.bounds_min,surface.bounds_max,0.0f" in world_capture
model_capture_region=capture.split("auto draw_model=[&]",1)[1]
assert "probe_distance_limit" in model_capture_region
assert "reflection_probe_bounds_outside_range(" in model_capture_region
assert "sky_frustum" not in read("renderer/reflection_sky_capture.inc")
assert '" distance_limit=",probe_distance_limit' in capture
assert "things,things.size(),nullptr,nullptr" in capture
assert "dual_paraboloid_bounds_outside(" in capture
assert "std::sort(model_group_scratch.begin()" not in capture
assert "group_key(model_group_scratch[group_end])==key" in capture
assert "glMultiDrawElements(GL_TRIANGLES,counts.data()" in capture
assert "scene_preparations=" in capture
assert "multi_draw_batches=" in capture
assert "groups_bounds_culled=" in capture
# Off-disk nodes must exit before GPU cache lookup and state submissions.
model_capture=capture.split("auto draw_model=[&]",1)[1].split(
    "for(auto const &inst:offscreen.nodes)draw_model(inst);",1)[0]
assert model_capture.index("if(model_group_scratch.empty())return;") < model_capture.index(
    "gpu_3do_gl_cache.find(inst.id)")
assert "thing_range_culled=" in capture
assert "dual_paraboloid_outside_distance(" not in capture
assert "frustum.clip_far=probe_distance_limit>0.0f?1:0" not in capture
assert "reflection_capture_scene_far(focus)" in capture
assert "glEnable(GL_CLIP_DISTANCE0)" in capture
assert "glEnable(GL_CLIP_DISTANCE1)" in capture
assert "glEnable(GL_CLIP_DISTANCE2)" not in capture
assert "paraboloid_capture_program" in renderer
assert "GL_CLIP_DISTANCE0" in read("renderer/offscreen_capture_target.hpp")
assert "dual_paraboloid_capture.geom" in read("renderer/planar_reflection_capture_shader.hpp")
assert "gl_ClipDistance[0]" in read("assets/shaders/dual_paraboloid_capture.geom")
assert "gl_ClipDistance[2]" not in read("assets/shaders/dual_paraboloid_capture.geom")
assert "capture_sky_mode" not in read("assets/shaders/dual_paraboloid_capture.geom")
assert "gl_FragDepth=0.999999" in read("assets/shaders/dual_paraboloid_capture.frag")
assert "gl_FragDepth" in read("assets/shaders/dual_paraboloid_capture.frag")
assert "paraboloid" in read("renderer/reflection_sky_capture.inc")
