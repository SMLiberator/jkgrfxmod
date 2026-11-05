"""Guard native Static.jkl savegame restore patch against accidental removal.

The x86 addresses and instructions were disassembled from the user-provided
jkgm(5).exe (SHA256 afb4605c8259075c151ad6b034b98562be87ebe90369d4d9abaee09b396ed50f),
not inferred from OpenJKDF2.
"""
from pathlib import Path
root=Path(__file__).resolve().parents[1]
main=(root/"renderer/main.cpp").read_text(encoding="utf-8")
header=(root/"renderer/saved_static_template.hpp").read_text(encoding="utf-8")
workflow=(root/".github/workflows/main.yml").read_text(encoding="utf-8")

assert '#include "saved_static_template.hpp"' in main
assert "bool request_native_save_template_hook()" in main
assert "request_native_save_template_hook();" in main
assert "native_save_template_hook_active" in main
assert "DetourAttach(" in main and "RealSaveTemplateGate,HookedSaveTemplateGate" in main
assert "DetourDetach(&(PVOID &)RealSaveTemplateGate,HookedSaveTemplateGate)" in main
assert "movzx eax, word ptr [esi]" in main # unsigned 0x8000-flagged index
assert "call resolve_saved_thing_template" in main
assert "mov dword ptr [esp+28], eax" in main # saved EAX in pushad frame
assert "push eax\n            push edi" in main # source template, then dst Thing
assert "mov eax, 0x004CDB70" in main # native SetThingBasedOn
assert "mov eax, 0x004F4A2E" in main # rejoin after native call
assert "mov eax, 0x004F4E9B" in main # native failure epilogue
assert "JK SAVE RESTORE: rejected template id=" in main
assert "restore_index_address=0x004F49F8U" in header
assert "current_world_global=0x00833A10U" in header
assert "static_world_global=0x00833A14U" in header
assert "world_template_count_offset=0xC8U" in header
assert "world_template_array_offset=0xD0U" in header
assert "thing_stride=0x2B0U" in header
assert "uint16_t(serialized_index&0x7FFFU)" in header
assert "bool static_world=false" in header
assert "out.static_world=(serialized_index&0x8000U)!=0U" in header
assert "uint16_t(actual)!=serialized_index" in header
assert "saved_static_template_test.exe" in workflow
