#!/usr/bin/env python3
"""Ensure GetThingJointNode COG handler matches JK.dll's ID->joint lookup."""

from pathlib import Path

root=Path(__file__).resolve().parents[1]
main=(root/"renderer/main.cpp").read_text(encoding="utf-8")
header=(root/"renderer/cog_thing_joint_node.hpp").read_text(encoding="utf-8")
project=(root/"renderer/renderer.vcxproj").read_text(encoding="utf-8")
audit=(root/"tests/verify_thing_node_binaries.py").read_text(encoding="utf-8")
doc=(root/"doc/cog_thing_joint_node.md").read_text(encoding="utf-8")

assert '#include "cog_thing_joint_node.hpp"' in main
assert 'cog_verb_entry{"getthingjointnode", cog_get_thing_joint_node}' in main
handler=main.split('void __cdecl cog_get_thing_joint_node(void *ctx)',1)[1].split(
    "// JK.dll's original handler compares attach_flags",1)[0]
assert handler.index("cog_pop_int(ctx)") < handler.index("cog_pop_thing(ctx)")
assert "cog_push_int(ctx,jkgm::cog_get_thing_joint_node(" in handler
assert "gpu_span_readable" in handler
for field in ("thing+0x128U","puppet)+0x1eU","0x174U","0x17cU",
              "0x1040U","joint>=10","puppet_id&0x8000U"):
    assert field in header,field
assert 'cog_thing_joint_node.hpp' in project
assert 'GetThingPuppet class ID' in audit
assert 'GetPuppetJointNode node slot' in audit
assert 'GetThingJointNode(thing, joint)' in doc
print("PASS GetThingJointNode registration, parameters, puppet class lookup and tests")
