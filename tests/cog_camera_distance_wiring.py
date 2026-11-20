#!/usr/bin/env python3
"""Native current-camera COG depth and distance wiring regressions."""
from pathlib import Path

root=Path(__file__).resolve().parents[1]
main=(root/"renderer/main.cpp").read_text(encoding="utf-8")
helper=(root/"renderer/cog_camera_distance.hpp").read_text(encoding="utf-8")
project=(root/"renderer/renderer.vcxproj").read_text(encoding="utf-8")
filters=(root/"renderer/renderer.vcxproj.filters").read_text(encoding="utf-8")
docs=(root/"doc/cog_camera_distance.md").read_text(encoding="utf-8")
for key in ("getthingcameradepth","getthingcameradistsq","getthingcameradist"):
    assert 'cog_verb_entry{"'+key+'",' in main,key
for name in ("cog_get_thing_camera_depth","cog_get_thing_camera_dist_sq",
             "cog_get_thing_camera_dist"):
    handler=main.split("void __cdecl "+name+"(void *ctx)",1)
    assert len(handler)==2,name
    assert "cog_get_thing_camera_measure(ctx," in handler[1].split("}",1)[0],name
assert "jkgm::engine_camera_detail::build_matches()" in main
assert "cog_pop_thing(ctx)" in main
assert "cog_push_float(ctx,value);" in main
assert "read_node_attachment_identity" in main
assert '0x0073a418U' in helper
assert 'thing+0xb8U' in helper
assert 'std::uintptr_t(camera)+0x08U' in helper
assert "if(measure==cog_camera_measure::depth)return y;" in helper
assert "if(measure==cog_camera_measure::distance_sq)return squared;" in helper
assert "if(measure==cog_camera_measure::distance)return std::sqrt(squared);" in helper
assert 'cog_camera_distance.hpp' in project
assert 'cog_camera_distance.hpp' in filters
assert 'GetThingCameraDepth' in docs
assert 'GetThingCameraDistSq' in docs
assert 'GetThingCameraDist' in docs
print("COG native camera depth and distance wiring passed")
