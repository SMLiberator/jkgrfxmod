"""Check the compressed-sound loader detour preserves its original ABI."""
from pathlib import Path

root=Path(__file__).resolve().parents[1]
main=(root/"renderer/main.cpp").read_text(encoding="utf-8")
header=(root/"renderer/native_compressed_sound_hook.hpp").read_text(encoding="utf-8")
hook=(root/"renderer/native_compressed_sound_hook.cpp").read_text(encoding="utf-8")
codec=(root/"renderer/compressed_sound_io.cpp").read_text(encoding="utf-8")
assert main.count('#include "native_compressed_sound_hook.hpp"')==1
assert hook.count('#include "native_compressed_sound_hook.hpp"')==1
assert "namespace jkgm::native_compressed_sound_hook" in header
assert "bool request();" in header and "void detach();" in header
assert "RealSithSoundLoad" not in main
assert "HookedSithSoundLoad" not in main
assert "0x004EF440U" in hook
assert "void* (__cdecl*)(char*,int)" in hook
assert "void* __cdecl HookedSithSoundLoad(char* filename,int eager)" in hook
assert "install_compressed_sound_file_services()" in hook
assert hook.index("install_compressed_sound_file_services()") < hook.index("return RealSithSoundLoad(filename,eager);")
assert "compressed_sound_executable_supported()" in hook
assert "DetourAttach(&(PVOID&)RealSithSoundLoad," in hook
assert "DetourDetach(&(PVOID&)RealSithSoundLoad," in hook
assert "DetourTransactionBegin(" not in hook and "DetourTransactionCommit(" not in hook
assert "bool native_compressed_sound_hook_active=false;" in main
assert "jkgm::native_compressed_sound_hook::request()" in main
assert "jkgm::native_compressed_sound_hook::detach();" in main
assert main.index("jkgm::native_compressed_sound_hook::detach();") < main.index("jkgm::uninstall_compressed_sound_file_services();")
assert "HostServices" in codec
print("Native compressed sound loader hook ABI, lifecycle and separation OK")
