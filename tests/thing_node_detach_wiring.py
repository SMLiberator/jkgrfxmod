#!/usr/bin/env python3
"""Source-contract regression for the DetachAttachedThings COG verb."""

from pathlib import Path

root = Path(__file__).resolve().parents[1]
main = (root / "renderer/main.cpp").read_text(encoding="utf-8")
binary_check = (root / "tests/verify_thing_node_binaries.py").read_text(encoding="utf-8")

assert 'node_attach_native_detach_children=0x004CE820U' in main
assert 'cog_verb_entry{"detachattachedthings", cog_detach_attached_things}' in main
assert 'bool native_detach_attached_things_supported()' in main
assert '0x2b,0xfe,0xff,0xff' in main  # native CALL DetachThing displacement

verb = main.split("void __cdecl cog_detach_attached_things(void *ctx)", 1)[1]
verb = verb.split("void __cdecl cog_get_thing_attach_normal", 1)[0]
assert "cog_pop_thing(ctx)" in verb
assert "native_detach_attached_things_supported()" in verb
assert "read_node_attachment_identity" in verb
assert "native_detach_children(parent)" in verb

detach_hook = main.split("void __cdecl HookedNativeThingDetach(void *child)", 1)[1]
detach_hook = detach_hook.split("bool attach_native_node_attachment_hooks()", 1)[0]
assert "get_node_attachment_registry().erase(" in detach_hook
assert "RealNativeThingDetach(child)" in detach_hook

assert '"sithThing_DetachAttachedThings", 0x004CE820' in binary_check
print("PASS DetachAttachedThings native entry, safety gate, COG registration and node sidecar cleanup")
