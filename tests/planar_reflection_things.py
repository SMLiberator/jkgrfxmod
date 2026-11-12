"""Read-only independent sector->Thing enumeration for future planar captures."""
from pathlib import Path
root=Path(__file__).resolve().parents[1]
read=lambda p:(root/p).read_text(encoding="utf-8")
header=read("renderer/planar_reflection_things.hpp")
renderer=read("renderer/renderer.cpp")
config=read("common/config.hpp")
parser=read("common/config.cpp")
json=read("assets/jkgm.json")
docs=read("doc/planar_reflections.md")
assert "bool experimental_planar_thing_discovery = false;" in config
assert 'j.contains("experimental_planar_thing_discovery")' in parser
assert '"experimental_planar_thing_discovery": false' in json
assert '#include "planar_reflection_things.hpp"' in renderer
assert 'collect_planar_reflection_things(read,world,scan)' in renderer
assert "gpu_3do_present_serial%120U==0U" in renderer
assert "PLANAR discovery world=" in renderer
assert "0x4cU,sector_count" in header
assert "0x50U,sector_base" in header
assert "0xbcU,thing_capacity" in header
assert "0xc4U,thing_base" in header
assert "sector_index)*0x98U" in header
assert "sector+0x30U,current" in header
assert "current)+0x60U,next" in header
assert "current)+0x5cU,owner" in header
assert "current)+0xc4U,render_type" in header
assert "current)+0xc8U,model" in header
assert "visited.insert(current).second" in header
assert "out={};out.error=reason" in header
assert "read_active_render_camera()" not in header
assert "read_active_rd_camera_view" not in header
assert "0x80000U" in header
assert "render_type!=1U" in header
assert "does NOT render" in docs
for path in ("renderer/renderer.vcxproj","renderer/renderer.vcxproj.filters"):
    assert "planar_reflection_things.hpp" in read(path)
print("Independent native Thing discovery guards OK")
