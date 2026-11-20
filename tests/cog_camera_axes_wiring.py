#!/usr/bin/env python3
"""The single-axis camera getters must match JK.dll's SithCamera RLU slots."""
from pathlib import Path

root=Path(__file__).resolve().parents[1]
main=(root/"renderer/main.cpp").read_text(encoding="utf-8")
hdr=(root/"renderer/cog_camera_axes.hpp").read_text(encoding="utf-8")
project=(root/"renderer/renderer.vcxproj").read_text(encoding="utf-8")
filters=(root/"renderer/renderer.vcxproj.filters").read_text(encoding="utf-8")
docs=(root/"doc/cog_camera_axes.md").read_text(encoding="utf-8")
for key,method,axis in (
    ("getcurrentcamerarvec","cog_get_current_camera_rvec","right"),
    ("getcurrentcameralvec","cog_get_current_camera_lvec","look"),
    ("getcurrentcamerauvec","cog_get_current_camera_uvec","up")
):
    assert f'cog_verb_entry{{"{key}", {method}}}' in main
    handler=main.split(f"void __cdecl {method}(void *ctx)",1)[1].split("}",1)[0]
    assert f"cog_get_current_camera_axis(ctx,jkgm::cog_camera_axis::{axis});" in handler
    assert method.replace("cog_get_","Get").replace("_", "") not in hdr
assert "jkgm::engine_camera_detail::build_matches()" in main
assert "cog_push_vector(ctx,vector.data());" in main
assert '0x008ee4e8U' in hdr
assert '0x008ec2c0U' in hdr
assert '0x4e0U' in hdr
assert '0x34U' in hdr
assert 'std::uintptr_t(component)*12U' in hdr
assert 'vec.data(),sizeof(vec)' in hdr
assert '<7' not in hdr  # range guarded with constant
assert 'cog_camera_axes.hpp' in project
assert 'cog_camera_axes.hpp' in filters
for name in ("GetCurrentCameraRVec","GetCurrentCameraLVec","GetCurrentCameraUVec"):
    assert name in docs
print("PASS JK.dll-compatible individual camera RLU getters")
