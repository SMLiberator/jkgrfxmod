"""Guard the five native template-spawn parent-call hooks and build wiring."""
from pathlib import Path
root=Path(__file__).resolve().parents[1]
source=(root/"renderer/native_spawn_parent_hook.cpp").read_text(encoding="utf-8")
header=(root/"renderer/native_spawn_parent.hpp").read_text(encoding="utf-8")
main=(root/"renderer/main.cpp").read_text(encoding="utf-8")
proj=(root/"renderer/renderer.vcxproj").read_text(encoding="utf-8")
filter_file=(root/"renderer/renderer.vcxproj.filters").read_text(encoding="utf-8")
doc=(root/"doc/native_spawn_parent.md").read_text(encoding="utf-8")

required_sites = {
    "0x004CDF7FU": "source_ebx_if_null",
    "0x004FBE46U": "owner_of_esi",
    "0x004ED3B6U": "owner_of_esi",
    "0x004D3B47U": "owner_of_esi",
    "0x004D41CFU": "owner_of_ebx",
}
for address,mode in required_sites.items():
    assert address in source and f"{address},parent_mode::{mode}" in source, address

assert "native_create=0x004CDCB0U" in source
assert "native_get_parent=0x004CC970U" in source
assert "GetModuleHandleA(nullptr)" in source
assert "code_matches(native_create,create_bytes)" in source
assert "code_matches(native_get_parent,root_bytes)" in source
assert "code_matches(site.call,expected)" in source
assert "while(applied)" in source and "originals[applied]" in source
assert "void uninstall()" in source
assert "pushad" in source and "popad" in source
assert "mov dword ptr [esp+14h],ebx" in source
assert "mov dword ptr [esp+34h],eax" in source
assert "call resolve_spawn_root" in source
assert "mov eax,04CDCB0h" in source
assert "root_or_source(source" in source
assert "choose_parent" in header
assert "native_spawn_parent_hook::install()" in main
assert "native_spawn_parent_hook::uninstall()" in main
assert main.index("native_spawn_parent_hook::install()") > main.index("LONG error = DetourTransactionCommit();", main.index("bool attach_hooks()"))
assert "native_spawn_parent_hook.cpp" in proj and "native_spawn_parent_hook.cpp" in filter_file
assert "native_spawn_parent.hpp" in proj
assert "fleshhit" in doc and "GetThingParent" in doc
assert "No extra Thing" in doc

print("Native template parent ownership + signature/restore contract OK")
