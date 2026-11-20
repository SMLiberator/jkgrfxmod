"""Material pack compiler must preserve MatCap and clearcoat fields."""
from pathlib import Path
r=Path(__file__).resolve().parents[1]
read=lambda p:(r/p).read_text(encoding="utf-8")
s=read("compile/main.cpp")
for name in ("matcap_map","matcap_surface_map","matcap_roughness",
             "matcap_factor","clearcoat_surface_map","clearcoat_normal_map",
             "clearcoat_strength","clearcoat_roughness","clearcoat_transmission"):
    assert f'std::optional<' in s and f'{name};' in s
    assert f'"{name}"' in s
    assert f'rv["{name}"]' in s
    assert f'                    "{name}",' in s, f"pack inheritance missing {name}"
assert 'get_convert_image(img_map,input,filename' in s
assert 'get_convert_linear_image(img_map,input,filename' in s
assert 'import_matcap_image("matcap_map",".matcap.png",false)' in s
assert '"matcap_surface_map",".matcap_surface.png",true' in s
assert '"clearcoat_normal_map",".clearcoat_normal.png",true' in s
assert 'v != "shared"' in s
assert 'v != "cube"' in s
assert 'v != "matcap"' not in s
assert 'matcap_factor RGB must be in [0,4]' in s
print("Matcap and clearcoat compiler contracts OK")
