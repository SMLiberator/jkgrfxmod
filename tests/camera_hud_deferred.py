"""Camera HUD GPU/native routing, deferred material ownership and alpha ordering."""
from pathlib import Path
import json

root=Path(__file__).resolve().parents[1]
read=lambda p:(root/p).read_text(encoding="utf-8")
config=read("common/config.hpp")
parser=read("common/config.cpp")
main=read("renderer/main.cpp")
render=read("renderer/renderer.cpp")
shader=read("assets/shaders/gpu_3do_preview.frag")
sample=json.loads(read("assets/jkgm.json"))

# The master switch really disables all visible GPU 3DOs, even Camera.
assert "bool enable_gpu_camera_hud = true;" in config
assert "bool camera_hud_depth_sort = true;" in config
assert 'j.contains("enable_gpu_camera_hud")' in parser
assert 'j.contains("camera_hud_depth_sort")' in parser
assert sample["enable_gpu_camera_hud"] is True
assert sample["camera_hud_depth_sort"] is True
queue=main[main.index("bool jkgm::queue_gpu_3do_mesh("):
           main.index("bool jkgm::",main.index("bool jkgm::queue_gpu_3do_mesh(")+5)] if "bool jkgm::" in main[main.index("bool jkgm::queue_gpu_3do_mesh(")+5:] else main[main.index("bool jkgm::queue_gpu_3do_mesh("):]
assert "if(active_native_model_camera_hud &&" in queue
assert "!the_config->use_gpu_cache ||" in queue
assert "!the_config->enable_gpu_camera_hud))" in queue
assert 'gpu_last_camera_hud_failure="native_HUD_renderer_selected";' in queue
assert "return false;" in queue
assert "RealNativeMesh(mesh,placement);" in main

# Native pointers may not be re-resolved at next-frame HUD presentation.
assert "deferred_gpu_hud_material_cache;" in render
draw=render[render.index("void draw_gpu_3do_preview("):
            render.index("// Deliberate world-cache isolation test.")]
assert "auto &mat_cache=post_exempt" in draw
assert "? deferred_gpu_hud_material_cache" in draw
assert ": gpu_frame_material_cache;" in draw
assert "if(post_exempt) continue;" in draw
assert draw.index("if(post_exempt) continue;") < draw.index("resolve_gpu_3do_material(group)")
assert "if(post_exempt && tex.albedo_source==0)" in draw

capture=render[render.index("// Preserve Camera Thing geometry, camera AND"):
               render.index("// Procedural particle clouds and camera-facing")]
assert "deferred_gpu_hud_material_cache.clear();" in capture
assert "auto found=gpu_frame_material_cache.find(key);" in capture
assert "deferred_gpu_hud_material_cache.emplace(" in capture
assert "deferred_gpu_3do_instances.push_back(std::move(inst));" in capture
assert "gpu_3do_present_serial%120U==0U" in capture
assert "hud_missing_mats" in capture
assert render.index("draw_game_transparency_pass(trimdl, posterize_lighting);") < render.index("// Preserve Camera Thing geometry, camera AND")
# Deferred copies must hold texture-cache references, not just raw GL
# texture_view names. A native MAT Load can otherwise overwrite a released
# image in place before the next present() call.
assert "void retain_deferred_gpu_hud_material(" in render
assert "void clear_deferred_gpu_hud_materials()" in render
assert "retain_deferred_gpu_hud_material(found->second);" in capture
assert "out.albedo2_id=to_albedo;" in render
assert "out.emission_id=surface->emissive_map;" in render
for field in ("albedo_id", "albedo2_id", "emission_id"):
    assert f"texture_cache->retain_srgb_texture(*m.{field})" in render
    assert f"texture_cache->release_srgb_texture(*m.{field})" in render
assert render.count("clear_deferred_gpu_hud_materials();")>=5
assert render.count("deferred_gpu_hud_material_cache.clear();")==1
assert "clear_deferred_gpu_hud_materials();" in render[
    render.index("void draw_post_exempt()"):
    render.index("// Called ONLY by the game's original Screenshot")
]

# For a flickering transparent panel, disabling depth sorting keeps
# captured mesh/face order; the ordinary 3DO alpha sort is unchanged.
assert "if(transparent && !(post_exempt &&" in draw
assert "!the_config->camera_hud_depth_sort))" in draw
assert "if(a.depth==b.depth) return a.order<b.order;" in draw
assert "return a.depth>b.depth;" in draw
assert "draw_gpu_3do_preview(true,true);" in render
assert "if(camera_hud_thing!=0)" in shader
assert "gl::disable(gl::capability::depth_test);" in render

print("Camera HUD native routing and deferred alpha diagnostic checks passed")
