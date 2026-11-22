"""Verify GPU timing stage boundaries and nonblocking reflection profiling."""
from pathlib import Path

root=Path(__file__).resolve().parents[1]
renderer=(root/"renderer/renderer.cpp").read_text(encoding="utf-8")
hemi=(root/"renderer/hemi_reflection_capture.inc").read_text(encoding="utf-8")
profile=(root/"renderer/presentation_gpu_profile.hpp").read_text(encoding="utf-8")
assert "GL_QUERY_RESULT_AVAILABLE" in profile
assert "if(ready!=GL_TRUE)" in profile
assert "glGetQueryObjectui64v" in profile

assert "presentation_gpu_profile opaque_detail_gpu_queries;" in renderer
assert "presentation_gpu_profile capture_gpu_queries;" in renderer
assert "opaque_detail_gpu_queries.release();" in renderer
assert "capture_gpu_queries.release();" in renderer
capture=renderer.split('auto profile_capture=[&](size_t stage,auto &&capture)',1)[1]
for label,needle in enumerate(('render_camera_to_mat_captures();',
                               'render_planar_capture();',
                               'render_hemi_capture();')):
    assert f'profile_capture({label}U,' in capture
    assert needle in capture
assert capture.index('profile_capture(2U,') < capture.index(
    'scene_gpu_queries.timestamp(0U,true)')
assert 'capture_gpu_queries.begin(capture_completed)' in renderer
assert 'capture_gpu_queries.end();' in renderer
assert 'PERF capture_gpu_us photo=' in renderer
assert 'PERF capture_cpu_us_avg photo=' in renderer
assert 'PERF reflection_refreshes_completed shared=' in renderer
assert '++reflection_refreshes_window[candidate.is_global?3U:' in hemi
gbuf=renderer.split('void draw_game_gbuffer_pass(',1)[1].split(
    '// One MAT binder shared',1)[0]
assert gbuf.index('opaque_detail_gpu_queries.timestamp(0U,true)') < gbuf.index(
    'draw_game_opaque_into_gbuffer(') < gbuf.index(
    'opaque_detail_gpu_queries.timestamp(0U,false)')
assert gbuf.index('opaque_detail_gpu_queries.timestamp(1U,true)') < gbuf.index(
    'frame_ao->update_ssao_texture(') < gbuf.index(
    'opaque_detail_gpu_queries.timestamp(1U,false)')
assert gbuf.index('opaque_detail_gpu_queries.timestamp(2U,true)') < gbuf.index(
    'draw_game_opaque_composite()') < gbuf.index(
    'render_opaque_screen_reflections()') < gbuf.index(
    'opaque_detail_gpu_queries.timestamp(2U,false)')
assert 'PERF opaque_detail_gpu_us geometry=' in renderer
assert "struct shared_capture_profile {" in renderer
assert "PERF shared_capture_cpu_us search_per_frame=" in renderer
assert "PERF shared_capture_batches per_refresh_models=" in renderer
assert "PERF shared_capture_gl_sample call_us=" in renderer
assert "PERF shared_capture_opaque_order sortable_ranges=" in renderer
assert '#include "reflection_capture_batch_order.hpp"' in renderer
assert "order_reflection_capture_opaque_spans(" in hemi
assert "if((group->face_type&0x2U)!=0U)return false;" in hemi
assert "!mat.alpha && !mat.masked && !mat.dithered" in hemi
assert "return a<b; // stable authored tie within geometry" in hemi
assert "shared_capture_profile_window={};" in renderer
assert "auto const shared_search_started=" in hemi
assert "diag.search_ns+=elapsed_profile_ns(" in hemi
assert "shared_capture_profile_window.cpu_ns[0]" in hemi
assert "diag.cpu_ns[1]+=elapsed_profile_ns(" in hemi
assert "shared_capture_profile_window.cpu_ns[2]" in hemi
assert "shared_capture_profile_window.cpu_ns[3]" in hemi
assert "shared_capture_profile_window.cpu_ns[4]" in hemi
assert "diag.cpu_ns[5]+=elapsed_profile_ns(" in hemi
assert "diag.cpu_ns[6]+=elapsed_profile_ns(" in hemi
assert "++diag.attempts;" in hemi
assert "if(all_faces_ready)++diag.completed;" in hemi
assert "++shared_capture_profile_window.groups_checked;" in hemi
assert "++shared_capture_profile_window.groups_selected;" in hemi
assert "++diag.model_single_batches;" in hemi
assert "++diag.model_small_batches;" in hemi
assert "++diag.model_large_batches;" in hemi
assert "(diag.model_batches&31U)==0U" in hemi
assert "++diag.sampled_gl_calls;" in hemi
# Main-camera/shadow draw paths already touch this timestamp. Reflection
# captures must do so too, or their GL meshes are evicted every cache sweep
# despite still appearing in every six-face capture.
capture_mesh=hemi.split("auto it=gpu_3do_gl_cache.find(inst.id);",1)[1]
assert capture_mesh.index(
    "gpu_3do_last_drawn[inst.id]=gpu_3do_present_serial;"
) < capture_mesh.index("shader.set_modelview(modelview,true,")
assert "gpu_3do_gl_cache.size()>=8192U" in capture_mesh
assert "gpu_3do_present_serial-used->second>256U" in renderer
assert "++shared_capture_profile_window.world_batches;" in hemi
assert hemi.index("diag.search_ns+=") < hemi.index("bool const profile_shared=")
assert hemi.index("auto const sky_started=") < hemi.index(
    "draw_reflection_capture_sky(shader,camera,view,frustum,")
assert hemi.index("auto const models_started=") < hemi.index(
    "for(auto const &inst:offscreen.nodes)draw_model(inst);")
print("Reflection/opaque GPU timers and Shared capture CPU attribution contracts OK")
