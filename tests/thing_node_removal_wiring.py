#!/usr/bin/env python3
"""Contract check: native inline removal cannot strand 0x80 ThingNode flags."""

from pathlib import Path

base = Path(__file__).resolve().parents[1]
source = (base / "renderer/main.cpp").read_text(encoding="utf-8")
header = (base / "renderer/thing_node_attachment.hpp").read_text(encoding="utf-8")
audit = (base / "tests/verify_thing_node_binaries.py").read_text(encoding="utf-8")

pre = source.split("void detach_native_destroyed_children_pre_update()", 2)
assert len(pre) >= 2
assert "void detach_native_children_from_parent(" in source
assert "native_detach_attached_things_supported()" in source
assert "node_attach_native_detach_children" in source

update = source.split("void __cdecl HookedNativeThingUpdate(float seconds,int milliseconds)",1)[1]
before_native = update.split("RealNativeThingUpdate(seconds,milliseconds)",1)[0]
assert "detach_native_destroyed_children_pre_update();" in before_native
assert "native_thing_requires_child_detach(kind,flags)" in source
assert "detach_native_children_from_parent(" in source

post = update.split("RealNativeThingUpdate(seconds,milliseconds)",1)[1]
post = post.split("// Sith's removal path",1)[0]
assert "native_orphan_node_link_matches(" in post
assert "rec.child+0x34),0,0x28U" in post
assert "rec.child+0x6c),0,0x08U" in post
assert "get_node_attachment_registry().erase(rec.child)" in post

assert "native_thing_destroyed=0x2U" in header
assert '"update inlined destroyed branch", 0x004CC4E5' in audit
assert '"update clears Thing signature", 0x004CC5A8' in audit
print("PASS native inline removal preflight and orphan node flags cleanup")
