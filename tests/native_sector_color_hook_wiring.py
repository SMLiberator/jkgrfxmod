"""Source contract for isolated native palette hook and Detours ownership."""
from pathlib import Path

root=Path(__file__).resolve().parents[1]
main=(root/"renderer/main.cpp").read_text(encoding="utf-8")
header=(root/"renderer/native_sector_color_hook.hpp").read_text(encoding="utf-8")
impl=(root/"renderer/native_sector_color_hook.cpp").read_text(encoding="utf-8")

assert main.count('#include "native_sector_color_hook.hpp"')==1
assert impl.count('#include "native_sector_color_hook.hpp"')==1
assert "namespace jkgm::native_sector_color_hook" in header
assert "bool request(bool gpu_cache_enabled);" in header
assert "void detach();" in header
assert "RealNativeColorEffects" not in main
assert "HookedNativeColorEffects" not in main
assert 'reinterpret_cast<native_color_effect_fn>(0x0043AE60U)' in impl
assert "if(!gpu_cache_enabled)return false;" in impl
assert "0x56,0x8b,0x74,0x24,0x08,0x57,0xb9,0x0a" in impl
assert "std::memcmp(fn,signature,sizeof(signature))!=0" in impl
assert "probe_native_sector_palette(" in impl
assert "neutral.tint[ch]-=probe.sector_tint[ch];" in impl
assert "return RealNativeColorEffects(&neutral);" in impl
assert "DetourAttach(&(PVOID&)RealNativeColorEffects," in impl
assert "DetourDetach(&(PVOID&)RealNativeColorEffects," in impl
assert "DetourTransactionBegin(" not in impl
assert "DetourTransactionCommit(" not in impl
assert "bool native_sector_effect_hook_active=false;" in main
assert "jkgm::native_sector_color_hook::request(" in main
assert "jkgm::native_sector_color_hook::detach();" in main
assert "the_config && the_config->use_gpu_cache" in main
assert "native_sector_palette_hook_installed().store(" in main
assert "native_sector_palette_mode().store(0" in main
print("Native palette Detours extraction, ABI and tint policy verified")
