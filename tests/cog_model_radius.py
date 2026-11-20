"""Contract tests for custom COG radius setters and verified JK.dll ABI."""
from pathlib import Path
import re

root = Path(__file__).resolve().parents[1]
src = (root / "renderer/main.cpp").read_text(encoding="utf-8")

# The supplied JK.dll getters (SHA-256 d7fa73fa1e2fcb03fd1353033950691804c83ea902b4b26ba4099d9983916d14):
#   GetModelRadius      0x100D2B40: PopModel3 at 0x004E3140, float [model+0x5C].
#   GetModelMeshRadius  0x100D2B70: PopInt then PopModel3, use geoset 0,
#                                    stride 0x70 and float [mesh+0x54].
assert "constexpr uintptr_t pop_model_address = 0x4E3140;" in src
assert "cog_pop_model_type const cog_pop_model" in src

def handler(name):
    return src.split(f"void __cdecl {name}(void *ctx)", 1)[1].split("\n    }", 1)[0]

whole = handler("cog_set_model_radius")
mesh = handler("cog_set_model_mesh_radius")

assert whole.index("cog_pop_float(ctx)") < whole.index("cog_pop_model(ctx)")
assert "cog_write_model_radius_field(base+0x5cU,radius)" in whole
assert mesh.index("cog_pop_float(ctx)") < mesh.index("cog_pop_int(ctx)") < mesh.index("cog_pop_model(ctx)")
assert "read_object(base+0x44U,num_geos)" in mesh
assert "read_object(base+0x24U,mesh_count)" in mesh
assert "read_object(base+0x28U,meshes)" in mesh
assert "uint64_t(uint32_t(mesh_index))*0x70U+0x54U" in mesh
assert "uint32_t(mesh_index)>=mesh_count" in mesh

write = src.split("bool cog_write_model_radius_field(uintptr_t address,float radius)", 1)[1].split("\n    }", 1)[0]
assert "std::isfinite(radius)" in write and "radius<0.0f" in write
assert "matches_tested_executable()" in write
assert "PAGE_READWRITE" in write and "PAGE_WRITECOPY" in write
assert "VirtualQuery(" in write and "std::memcpy(" in write
assert 'cog_verb_entry{"setmodelradius", cog_set_model_radius}' in src
assert 'cog_verb_entry{"setmodelmeshradius", cog_set_model_mesh_radius}' in src
assert not re.search(r"rdModel3_CalcRadii|SetThingSize|SetThingMoveSize", whole+mesh)
print("COG model radius setter ABI/source contracts passed")
