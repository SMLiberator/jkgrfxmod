"""Check the extracted native JK.dll mesh revision tracker keeps its ABI.

This intentionally checks wiring/registration; Windows CI builds the actual
separate translation unit. No executable hooks or memory addresses are mocked.
"""
from pathlib import Path
import re

root = Path(__file__).resolve().parents[1]
main = (root / "renderer/main.cpp").read_text(encoding="utf-8")
header = (root / "renderer/native_mesh_revision.hpp").read_text(encoding="utf-8")
impl = (root / "renderer/native_mesh_revision.cpp").read_text(encoding="utf-8")

assert main.count('#include "native_mesh_revision.hpp"') == 1
assert impl.count('#include "native_mesh_revision.hpp"') == 1
assert "namespace mesh_revision {" not in main
assert impl.count("namespace mesh_revision {") == 1
for signature in (
    "using handler = void(__cdecl *)(void *);",
    "using registrar = void(__cdecl *)(void *,handler,char const *);",
    "void __cdecl register_mutator(",
    "void begin(registrar fn);",
    "void finish();",
    "bool active();",
    "uint64_t current();",
    "uint64_t material_current();",
):
    assert signature in header, signature
for original in (
    "mesh_revision::begin(",
    "mesh_revision::register_mutator",
    "mesh_revision::finish(",
    "mesh_revision::current(",
    "mesh_revision::active(",
):
    assert original in main, original

# All mutators and their ordinal wrapper mappings are unchanged.
expected = (
    "setmodelmesh", "clonemodel", "changemodelmaterial",
    "replacemodelmaterial", "appendthingmaterial", "appendmodelmaterial",
    "appendthingemptymesh", "appendmodelemptymesh", "appendmodelnode",
    "replacemodelmeshmaterial", "clearmodel", "clearmodelmesh",
    "overwritemodel", "reloadmodel", "setmodelnodemesh", "swapmodelmesh",
)
names = impl.split("names{{", 1)[1].split("}};", 1)[0]
found = tuple(re.findall(r'"([a-z]+)"', names))
assert found == expected, (found, expected)
assert "constexpr std::size_t mutator_count=16U;" in header
assert "originals[I](ctx);" in impl
assert "++generation;" in impl
assert "original_registrar(ctx,wrappers[i],name);" in impl
assert "original_registrar(ctx,fn,name);" in impl
assert "complete=registered_mask==((1U<<mutator_count)-1U);" in impl
assert "return complete;" in impl
assert "untracked_write" not in impl
assert "gpu_mesh_integrity_audit_interval" not in main
assert "gpu_mesh_integrity_audit_frame" not in main
assert "gpu_frame_serial%32U" not in main
assert "mesh_revision::report_untracked" not in main
assert "integrity audit UNTRACKED source change:" not in main
assert "last_audit_frame" not in main
assert "if(effect&source_full)++source_generation;" in impl
assert "if(effect&source_materials)++material_generation;" in impl
assert "gpu_mesh_cache_apply_mutations()" not in main
assert "take_pending_source_effects" not in impl
assert "take_pending_source_effects" not in main
assert "for(auto it=gpu_mesh_cache.begin()" not in main
assert "gpu_mesh_lazy_material_changed(" in main
assert main.count("gpu_mesh_lazy_material_changed(")==3
assert main.count("gpu_mesh_dirty_source_readable(")==3
assert "material_checked_generation=material_generation;" in main
assert "mesh_revision::source_current()" in main
assert "mesh_revision::material_current()" in main
print("Separate JK.dll COG mesh revision module wiring and ABI preserved")
