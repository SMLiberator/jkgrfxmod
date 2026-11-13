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
assert "excluded_geo_mode" in header
assert "current)+0xf8U" in header
assert "current_geo<3" in header
assert "render_type!=1U" in header
assert "does NOT render" in docs
assert "planar_reflection_workset.hpp" in renderer
assert "build_planar_reflection_workset(" in renderer
assert "uncovered_things=" in renderer
# Other features (e.g. 3DO occlusion) may share this identity-capture guard.
# Verify that planar discovery enables owner capture without depending on
# whether its condition happens to be the final term of a Boolean OR.
native=read("renderer/main.cpp")
start=native.index("bool const have_thing_flags=")
end=native.index("owner_key=(uint64_t(owner)<<32)|signature;",start)
identity_guard=native[start:end]
assert "jkgm::read_native_model_thing_flags(read,rd,flags)" in identity_guard
assert "(the_config && the_config->experimental_planar_thing_discovery)" in identity_guard
assert "if(have_thing_flags)" in identity_guard
assert "read(rd+0x44U,&owner,4)" in identity_guard
assert "read(uintptr_t(owner)+0x74U,&signature,4)" in identity_guard
for path in ("renderer/renderer.vcxproj","renderer/renderer.vcxproj.filters"):
    assert "planar_reflection_things.hpp" in read(path)
print("Independent native Thing discovery guards OK")
