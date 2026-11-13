"""Source-contract regression for bounded main-camera GPU Thing occlusion."""
from pathlib import Path
import json,re
root=Path(__file__).resolve().parents[1]
read=lambda p:(root/p).read_text(encoding="utf-8")
header=read("renderer/gpu_thing_occlusion.hpp")
math=read("renderer/gpu_thing_occlusion_math.hpp")
renderer=read("renderer/renderer.cpp")
shader=read("assets/shaders/gpu_thing_occlusion.vert")
frag=read("assets/shaders/gpu_thing_occlusion.frag")
config=read("common/config.cpp")
settings=json.loads(read("assets/jkgm.json"))
assert settings["experimental_3do_occlusion_culling"] is False
assert settings["gpu_3do_occlusion_query_budget"]==24
assert settings["gpu_3do_occlusion_min_triangles"]==96
assert "gpu_3do_occlusion_query_budget must be between 1 and 64" in config
assert "gpu_3do_occlusion_min_triangles must be between 1 and 50000" in config
assert "glBeginQuery(GL_ANY_SAMPLES_PASSED" in header
assert "GL_QUERY_RESULT_AVAILABLE" in header
assert "consecutive_hidden>=2U" in header
assert "frame-e.result_frame<=4U" in header
assert "gpu_thing_occlusion_camera_compatible" in header
assert "gpu_thing_occlusion_same_bound" in header
assert "gpu_thing_occlusion_camera_inside" in header
assert "if(entries_.size()>=256U)return false;" in header
assert "glDrawArrays(GL_TRIANGLES,0,36)" in renderer
assert "glColorMask(GL_FALSE,GL_FALSE,GL_FALSE,GL_FALSE);" in renderer
assert "glDepthMask(GL_FALSE);" in renderer
assert "glDepthFunc(GL_LEQUAL);" in renderer
# Eligibility must follow the configurable threshold, not a hardcoded
# historical 500-triangle cutoff. The fixed query budget is unchanged.
assert "p.second.triangles>=500U" not in renderer
assert "triangles>=size_t(the_config->gpu_3do_occlusion_min_triangles)" in renderer
assert "owners_ge32=" in renderer and "owners_ge500=" in renderer
assert "query_gpu_thing_occlusion();" in renderer
assert "void query_gpu_thing_occlusion()" in renderer
assert "3DO OCCLUSION candidates=" in renderer
assert "queries.needs_retest(item.owner)" in renderer
assert "thing_occlusion_queries->hidden(" in renderer
assert "planar_reflection_radius_scale(basis)" in renderer
assert "thing+0xc8U" in renderer and "+0x5cU" in renderer
assert "shadow_owner" in renderer and "owning_thing_world_position" in renderer
assert "const int ids[36]" in shader
assert "gl_VertexID" in shader
assert "void main()" in frag
assert "animat" in math.lower()
assert not re.search(r"\bglFinish\s*\(",header+renderer)
assert "gpu_thing_occlusion.hpp" in read("renderer/renderer.vcxproj")
assert "gpu_thing_occlusion_math.hpp" in read("renderer/renderer.vcxproj")
assert "gpu_thing_occlusion.hpp" in read("renderer/renderer.vcxproj.filters")
assert "gpu_thing_occlusion_math.hpp" in read("renderer/renderer.vcxproj.filters")
# Per-Thing suppression must run BEFORE mesh-local transform allocation and
# material group traversal. Query only AFTER opaque scene 3DO depth is ready.
draw=renderer.split("void draw_gpu_3do_preview(",1)[1].split(
    "void draw_game_opaque_into_gbuffer(",1)[0]
assert draw.index("thing_occlusion_queries->hidden(")<draw.index(
    "modelviews.push_back(gpu_3do_native_modelview(")
opaque=renderer.split("void draw_game_opaque_into_gbuffer(",1)[1].split(
    "void query_gpu_thing_occlusion()",1)[0]
assert opaque.index("draw_gpu_3do_preview(false);")<opaque.index(
    "query_gpu_thing_occlusion();")
# Never alter offscreen capture eligibility or native rdModel3 callback.
offscreen=renderer.split("void render_planar_capture()",1)[1].split(
    "void present_game()",1)[0]
assert "thing_occlusion_queries->hidden(" not in offscreen
main=read("renderer/main.cpp")
assert "thing_occlusion_queries" not in main
# The native draw hook records per-Thing identity/world origin ONLY when
# this feature is enabled (or when an existing feature needs it).
assert "the_config->experimental_3do_occlusion_culling" in main
assert "own_world_position_valid=read(" in main
assert "active_native_shadow_owner=owner_key;" in main
print("3DO GPU Thing occlusion source contracts passed")
