"""Source-contract regression for experimental main-camera GPU Thing occlusion."""
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
assert settings["experimental_3do_native_occlusion_culling"] is False
assert "experimental_3do_native_occlusion_culling" in config
assert "experimental_3do_native_occlusion_culling" in read("common/config.hpp")

assert settings["gpu_3do_occlusion_query_budget"]==128
assert settings["gpu_3do_occlusion_debug_view"]==0
assert settings["gpu_3do_occlusion_min_triangles"]==0
assert settings["gpu_3do_occlusion_proxy_mode"]==0
assert "gpu_3do_occlusion_proxy_mode must be between 0 and 2" in config
assert "proxy_mode=the_config->gpu_3do_occlusion_proxy_mode" in renderer
assert "queries.set_proxy_mode(unsigned(proxy_mode))" in renderer
assert "program.set_proxy_mode(proxy_mode)" in renderer
assert "gpu_thing_occlusion_project_sphere_rect(" in renderer
assert "glDrawArrays(GL_TRIANGLES,0,6)" in renderer
assert "glDrawArrays(GL_TRIANGLES,0,36)" in renderer
assert "bound.radius=radius*scale;" in renderer
assert "if(proxy_mode!=0)" in renderer
assert "program.set_rect(rect,proxy_mode);" in renderer
assert "program.set_rect(item.rect,proxy_mode);" in renderer
assert "rect_ndc" in shader and "rect_forward" in shader
assert "rect_corners[6]" in shader and "proxy_mode!=0" in shader
assert "proxy_mode==2?r.center_forward:r.near_forward" in header
assert "struct gpu_thing_occlusion_screen_rect" in math
# CPU rectangle projection MUST follow the exact rdMatrix34 transform that
# the 3D box and cached models use in their GLSL vertex shaders.
assert "gpu_3do_native_transform_point(" in math
assert "double const x=view_center[0],depth=view_center[1],z=view_center[2];" in math
assert "out.near_forward=float(depth-r);" in math
assert "out.center_forward=float(depth);" in math
assert "rect.center_forward==5.0f" in read("tests/gpu_thing_occlusion_math.cpp")
assert "void set_rect(gpu_thing_occlusion_screen_rect const &r," in header
assert "rotated_sphere,yaw_view" in read("tests/gpu_thing_occlusion_math.cpp")
assert "pitched_sphere,pitch_view" in read("tests/gpu_thing_occlusion_math.cpp")
assert "thing_occlusion_gpu_timers.timestamp(0U,true)" in renderer
assert "thing_occlusion_gpu_timers.timestamp(0U,false)" in renderer
assert "3DO OCCLUSION proxy_perf mode=" in renderer

assert "gpu_3do_occlusion_query_budget must be between 0 and 256" in config
assert "gpu_3do_occlusion_debug_view must be between 0 and 2" in config
assert "gpu_3do_occlusion_debug_view" in read("common/config.hpp")
assert "gpu_3do_occlusion_query_budget==0" in renderer
assert "draw_gpu_thing_occlusion_debug();" in renderer
assert "glPolygonMode(GL_FRONT_AND_BACK" in renderer
assert "thing_occlusion_debug_gpu_timers.timestamp(stage,false)" in renderer
assert "debug_gpu_us_avg=" in renderer
assert '" occlusion_lookup_hits_avg="' in renderer
assert "profile_window.native_occlusion_lookup_hits/frames" in renderer
assert "thing_occlusion_visibility_ns_window" in renderer
assert "thing_occlusion_poll_ns_window" in renderer
assert "thing_occlusion_prepare_ns_window" in renderer
assert "thing_occlusion_gpu_timers.timestamp(stage,false)" in renderer
assert "layout(location=0) out vec4 out_color;" in frag
assert "gpu_3do_occlusion_min_triangles must be between 0 and 50000" in config
assert "glBeginQuery(GL_ANY_SAMPLES_PASSED" in header
assert "GL_QUERY_RESULT_AVAILABLE" in header
assert "consecutive_hidden>=1U" in header
assert "frame-e.result_frame<=12U" in header
assert "gpu_thing_occlusion_camera_compatible" in header
assert "gpu_thing_occlusion_same_bound" in header
assert "gpu_thing_occlusion_camera_inside" in header
assert "debug_result_state(uint64_t owner)" in header
assert "if(entries_.size()>=1024U)return false;" not in header
assert "glDrawArrays(GL_TRIANGLES,0,36)" in renderer
assert "glColorMask(GL_FALSE,GL_FALSE,GL_FALSE,GL_FALSE);" in renderer
assert "glDepthMask(GL_FALSE);" in renderer
assert "glDepthFunc(GL_LEQUAL);" in renderer
# Eligibility must follow the configurable threshold, not a hardcoded
# historical 500-triangle cutoff. A zero query budget is unlimited.
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
# Native rejection must not affect reflected-camera captures, animation
# simulation, or GL query execution. Live Thing and camera validation is
# required before the early return; hidden Things must be re-queried.
offscreen=renderer.split("void render_planar_capture()",1)[1].split(
    "void present_game()",1)[0]
assert "thing_occlusion_queries->hidden(" not in offscreen
main=read("renderer/main.cpp")
bridge=read("renderer/native_occlusion_bridge.hpp")
assert "thing_occlusion_queries" not in main
assert "native_occlusion_enabled" in main
assert "the_config->experimental_3do_native_occlusion_culling;" in main
assert "experimental_planar_reflection_capture" not in main.split(
    "bool const native_occlusion_enabled=",1)[1].split(";",1)[0]
assert "owner_type>1U && owner_type!=10U" in main
assert "!(flags&0x20U)" in main
assert "bound.center=own_world_position;" in main
assert "needs_decal_pose" not in main.split(
    "bool const native_occlusion_enabled=",1)[1].split(
    "// A nominally static",1)[0]
assert "gpu_thing_occlusion_camera_compatible(" in main
assert "gpu_thing_occlusion_camera_inside(" in main
assert "native_totals.model_occlusion_lookups" in main
assert "native_totals.model_occlusion_ns" in main
assert main.index("bridge.register_candidate(candidate);") < main.index(
    "return 0;", main.index("bridge.register_candidate(candidate);"))
assert main.index("bridge.register_candidate(candidate);") < main.index(
    "int const result=RealNativeModel(thing,placement);")
assert "get_native_occlusion_bridge().take_candidates()" in renderer
assert "thing_occlusion_cost_history" in renderer
assert "queries.confirmed_hidden(" in renderer
assert "gpu_3do_present_serial,model" in renderer
assert "published_world" in bridge and "std::atomic_load_explicit(" in bridge
assert "std::atomic_store_explicit(" in bridge
assert "gpu_thing_occlusion_same_bound(" in bridge
assert "std::chrono::milliseconds(500)" in bridge
assert "model_occlusion_skipped_avg=" in renderer
assert "native_occlusion_bridge.hpp" in read("renderer/renderer.vcxproj")
assert "native_occlusion_bridge.hpp" in read("renderer/renderer.vcxproj.filters")
# The native draw hook records per-Thing identity/world origin ONLY when
# this feature is enabled (or when an existing feature needs it).
assert "the_config->experimental_3do_occlusion_culling" in main
assert "own_world_position_valid=read(" in main
assert "active_native_shadow_owner=owner_key;" in main
print("3DO GPU Thing occlusion source contracts passed")
