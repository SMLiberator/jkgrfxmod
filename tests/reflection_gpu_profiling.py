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
print("Reflection/opaque GPU timers: nonblocking rings, stage boundaries, and refresh counts OK")
