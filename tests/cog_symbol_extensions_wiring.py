"""Native parser, loader, and initialization bridge regression contracts."""
from pathlib import Path
root=Path(__file__).resolve().parents[1]
native=(root/"renderer/native_cog_symbol_extensions.cpp").read_text(encoding="utf-8")
helper=(root/"renderer/cog_symbol_extensions.hpp").read_text(encoding="utf-8")
main=(root/"renderer/main.cpp").read_text(encoding="utf-8")
project=(root/"renderer/renderer.vcxproj").read_text(encoding="utf-8")
doc=(root/"doc/cog_symbol_extensions.md").read_text(encoding="utf-8")
assert "sprite_ref=14, soundclass_ref=15, puppet_ref=16" in helper
assert "native_refs_begin=0x1b4U" in native
assert "native_refs_count=0x1db4U" in native
assert "native_ref_stride=0x38U" in native
assert "0x004fcf50U" in native
assert "0x004df6a0U" in native
assert "0x004fdd70U" in native
assert "fix_vector_declaration" in native
assert "native_parse_ref(script,type,descriptions)" in native
assert "HookedCogResolver" in native
assert "write_word(addr+4U,3U)" in native
assert "find_loaded_index" in native
assert "w==stat ? 0x8000 : 0" in native
assert "VirtualProtect" in native and "FlushInstructionCache" in native
assert "DetourAttach(&(PVOID&)RealCogResolver,HookedCogResolver)" in native
assert "call process_extra_cog_symbols" in native
assert "mov eax,0431880h" in native
assert "jkgm::native_cog_symbol_extensions::request()" in main
assert "jkgm::native_cog_symbol_extensions::detach()" in main
assert "on_failed_commit()" in main
assert "native_cog_symbol_extensions.cpp" in project
assert "vector direction=(0/1/1) local" in doc
assert "vector direction=(0/1/1) local;" not in doc
print("COG declaration initialization, native resolver hook and no-semicolon docs OK")
