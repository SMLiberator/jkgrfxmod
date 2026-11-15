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
    "void report_untracked();",
):
    assert signature in header, signature
for original in (
    "mesh_revision::begin(",
    "mesh_revision::register_mutator",
    "mesh_revision::finish(",
    "mesh_revision::current(",
    "mesh_revision::active(",
    "mesh_revision::report_untracked(",
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
assert "constexpr size_t mutator_count=16U;" in impl
assert "originals[I](ctx);" in impl
assert "++generation;" in impl
assert "original_registrar(ctx,wrappers[i],name);" in impl
assert "original_registrar(ctx,fn,name);" in impl
assert "complete=registered_mask==((1U<<mutator_count)-1U);" in impl
assert "return complete && !untracked_write;" in impl
assert "if(untracked_write)return;" in impl
print("Separate JK.dll COG mesh revision module wiring and ABI preserved")
