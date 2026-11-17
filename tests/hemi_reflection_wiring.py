"""One camera-facing wide-angle probe source contracts."""
import json
from pathlib import Path
root=Path(__file__).resolve().parents[1]
read=lambda p:(root/p).read_text(encoding="utf-8")
settings=json.loads(read("assets/jkgm.json"))
assert settings["experimental_hemi_reflection_capture"] is False
assert settings["hemi_reflection_resolution_scale"]==0.25
mat=read("common/material.hpp")
parser=read("common/material_map.cpp")
config=read("common/config.cpp")
renderer=read("renderer/renderer.cpp")
capture=read("renderer/hemi_reflection_capture.inc")
shader=read("assets/shaders/gpu_3do_preview.frag")
units=read("renderer/texture_units.hpp")
assert "matcap, planar, hemi" in mat
assert 'v == "hemi"' in parser
assert "material_reflection_mode::hemi" in parser
assert "0.125f" in config and "hemi_reflection_resolution_scale" in config
assert '#include "hemi_reflection_camera.hpp"' in renderer
assert '#include "hemi_reflection_capture.inc"' in renderer
assert "render_hemi_capture();" in renderer
assert "std::unordered_set<uint64_t> acquired;" in capture
assert "planar_reflection_thing_owner_key(thing)==" in capture
assert "acquire_planar_reflection_offscreen_gpu(" in capture
assert "for(auto const &surface:cache.mesh.surfaces)" in capture
assert "mode!=renderer_mode::ingame" in capture
assert "hemi_capture_owner=selected_owner;" in capture
assert "hemi_capture_ready=true;" in capture
assert "hemi_capture_owner" in renderer
assert "hemi_capture_target->color_texture()" in renderer
assert "inst.shadow_owner==hemi_capture_owner" in renderer
assert "mode==5 && hemi_capture_info.z>0.5" in shader
assert "textureLod(hemi_reflection_map," in shader
assert "env=mix(env,capture.rgb" in shader
assert "roughness*log2" in shader and "fresnel" in shader
assert '{"hemi_reflection_map",23,sampler_kind::tex2d}' in units
assert "experimental_screen_space_reflections" in read("common/config.hpp")
assert "gpu_3do_preview" in units
assert "hemi_reflection_camera.hpp" in read("renderer/renderer.vcxproj")
assert "hemi_reflection_camera.hpp" in read("renderer/renderer.vcxproj.filters")
assert "hemi_reflection_capture.inc" not in read("renderer/renderer.vcxproj")
print("HEMI material, capture, and fallback contracts OK")
