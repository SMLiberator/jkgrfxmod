"""Source contracts for exact per-attribute cached-world upload tracking.

The C++ world_cache.cpp tests cover the dirty-range decisions and shadow
resynchronization; these checks protect integration with the GL draw path.
"""
from pathlib import Path

root=Path(__file__).resolve().parents[1]
read=lambda p:(root/p).read_text(encoding="utf-8")
render=read("renderer/renderer.cpp")
world=read("renderer/world_cache_gpu.hpp")
helper=read("renderer/world_cache_live_upload.hpp")
cpp=read("tests/world_cache.cpp")
project=read("renderer/renderer.vcxproj")

assert '#include "world_cache_live_upload.hpp"' in render
assert "plan_world_cache_live_upload(" in render
assert "record_world_cache_live_upload(values,uploaded,dirty);" in render
assert "if(dirty.empty())" in render
assert "if(dirty.full)" in render
assert "gl::buffer_sub_data(" in render
assert "gl::buffer_data(" in render
assert "skipped_buffers=" in render
assert "bytes_uv=" in render
assert "bytes_sector_tint=" in render

for suffix in ("light_rgb", "light_state", "sector_tint",
               "sector_cmp_tint", "uv_offset"):
    assert "uploaded_"+suffix in world
    assert "uploaded_"+suffix+".clear();" in world
    assert "uploaded_"+suffix+"=frame_"+suffix+";" in world
    assert "cache.uploaded_"+suffix in render

assert "std::memcmp(" in helper
assert "block_vertices=64U;" in helper
assert "plan.ranges.size()>12U" in helper
assert "plan.uploaded_vertices>total/4U" in helper
assert "uploaded.size()!=total" in helper
assert "if(!visible.valid" in helper
assert "if(span.count==0U)continue;" in helper
assert "uploaded=live;" in helper
assert "std::copy_n(" in helper
assert "record_world_cache_live_upload(live,uploaded,dirty);" in cpp
assert "uploaded[901U][1]==3.0f" in cpp
assert '<ClInclude Include="world_cache_live_upload.hpp" />' in project
print("World cached live-attribute streaming integration contracts passed")
