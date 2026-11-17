"""One camera-facing wide-angle probe source contracts."""
import json
from pathlib import Path
root=Path(__file__).resolve().parents[1]
read=lambda p:(root/p).read_text(encoding="utf-8")
settings=json.loads(read("assets/jkgm.json"))
assert settings["experimental_hemi_reflection_capture"] is False
assert settings["hemi_reflection_resolution_scale"]==0.25
assert settings["experimental_cube_reflection_capture"] is False
assert settings["cube_reflection_resolution_scale"]==0.25
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
assert "0.125f" in config and "hemi_reflection_resolution_scale" in config
assert "experimental_cube_reflection_capture" in config
assert "cube_reflection_resolution_scale" in config
assert "reflection_probe_max_owners" in config
assert "cube_reflection_update_interval" in config
assert settings["reflection_probe_max_owners"] == 4
assert settings["cube_reflection_update_interval"] == 0.0
assert '#include "hemi_reflection_camera.hpp"' in renderer
assert '#include "hemi_reflection_capture.inc"' in renderer
assert "render_hemi_capture();" in renderer
assert "std::unordered_set<uint64_t> acquired;" in capture
assert "planar_reflection_thing_owner_key(thing)==" in capture
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
