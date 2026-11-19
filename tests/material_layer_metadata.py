"""Non-rendering foundation for independent matcap/clearcoat material layers."""
from pathlib import Path

root=Path(__file__).resolve().parents[1]
read=lambda path:(root/path).read_text(encoding="utf-8")
model=read("common/material.hpp")
parser=read("common/material_map.cpp")
workflow=read(".github/workflows/main.yml")
doc=read("doc/material_layers.md")
for name in (
    "matcap_map","matcap_roughness","matcap_factor","matcap_surface_map",
    "clearcoat_strength","clearcoat_roughness","clearcoat_transmission",
    "clearcoat_surface_map","clearcoat_normal_map"
):
    assert name in model, name
    assert f'"{name}"' in parser, name
    assert name in doc, name
assert 'read_unit("clearcoat_strength",' in parser
assert "mat->clearcoat_surface_map?1.0f:0.0f" in parser
assert 'read_unit("clearcoat_transmission",1.0f)' in parser
assert 'read_unit("clearcoat_roughness",0.1f)' in parser
assert 'read_unit("matcap_roughness",0.0f)' in parser
assert "factor.size()!=3U" in parser
assert "std::isfinite(values[i])" in parser
assert '"matcap_strength"' not in parser
assert '"reflection_factor"' not in parser
assert 'material_reflection_mode { envmap, screen, fixed, matcap, planar, hemi, cube }' in model
assert "material_layer_metadata_test.exe" in workflow
