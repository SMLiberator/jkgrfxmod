"""Opaque geometry sub-stage GPU query attribution remains async and ordered."""
from pathlib import Path
root=Path(__file__).resolve().parents[1]
render=(root/"renderer/renderer.cpp").read_text(encoding="utf-8")
profile=(root/"renderer/presentation_gpu_profile.hpp").read_text(encoding="utf-8")
assert "template<size_t StageCount>" in profile
assert "using opaque_geometry_gpu_profile=basic_presentation_gpu_profile<6>;" in profile
assert "GL_QUERY_RESULT_AVAILABLE" in profile
assert "if(ready!=GL_TRUE)" in profile
assert "opaque_geometry_gpu_profile opaque_geometry_gpu_queries;" in render
assert "opaque_geometry_gpu_queries.release();" in render
assert "PERF opaque_geometry_gpu_us clear=" in render
assert "native_and_world=" in render
assert "msaa_resolve=" in render
gbuf=render.split("void draw_game_gbuffer_pass(",1)[1].split(
    "// One MAT binder shared",1)[0]
draw=render.split("void draw_game_opaque_into_gbuffer(",1)[1].split(
    "// Secondary capture is rendered BEFORE",1)[0]
assert 'opaque_geometry_gpu_queries.begin(geometry_completed)' in gbuf
assert 'opaque_geometry_gpu_queries.end();' in gbuf
assert gbuf.index('timestamp(0U,true)') < gbuf.index('frame_screen->begin_opaque_pass()') < gbuf.index('timestamp(0U,false)')
assert gbuf.index('timestamp(1U,true)') < gbuf.index('draw_game_opaque_into_gbuffer(')
assert gbuf.index('timestamp(4U,false)') < gbuf.index('frame_screen->end_opaque_pass()')
assert gbuf.index('timestamp(5U,true)') < gbuf.index('frame_screen->end_opaque_pass()') < gbuf.index('timestamp(5U,false)')
assert draw.index('draw_world_cache_preview();') < draw.index('timestamp(1U,false)')
assert draw.index('timestamp(2U,true)') < draw.index('gun_batch,')
assert draw.index('gun_transparent_batch,') < draw.index('timestamp(2U,false)')
assert draw.index('timestamp(3U,true)') < draw.index('draw_gpu_3do_preview(false);') < draw.index('timestamp(3U,false)')
assert draw.index('timestamp(4U,true)') < draw.index('query_gpu_thing_occlusion();') < draw.index('query_planar_receiver_visibility();')
print("Opaque geometry GPU substage timing remains nonblocking and ordered.")
