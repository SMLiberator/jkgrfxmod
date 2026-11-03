"""Keep projected dynamic decals across JKGM GeoSet/LOD changes."""
from pathlib import Path
root=Path(__file__).resolve().parents[1]
main=(root/"renderer/main.cpp").read_text(encoding="utf-8")
lod=(root/"renderer/sprite_decal_geoset.hpp").read_text(encoding="utf-8")
workflow=(root/".github/workflows/main.yml").read_text(encoding="utf-8")
assert "sprite_decal_geoset.hpp" in main
assert "enumerate_sprite_decal_lod_meshes(" in main
assert "read_sprite_decal_lod_node_pose(" in main
assert "read_sprite_decal_active_geoset(" in main
assert "read_sprite_decal_draw_geoset(" in main
assert "decal_receiver_geoset" in main
assert "native.geoset" in main
assert "receiver_model,source_mesh,source_pose,hit_geoset" in main
assert "read_sprite_decal_draw_geoset" in lod
assert "fragment.node_index=node.node;" in main
assert "fragment.geoset=node.geoset;" in main
assert "fragment.geoset=hit_geoset;" in main
assert "fragment.native_fallback=true;" in main
assert "decal_geoset_same_pose(" in main
assert "lod_projector,lod_worlds" in main
assert "accumulate_sprite_decal_normals(" in main
assert "sprite_decal_geometry_to_receiver_local(" in main
assert "fragment.geoset!=active_lod" in main
assert "moving_sprite_decal_receiver_alive(fragment)" in main
assert "0x70U" in lod
assert "0xb4U" in lod
assert "model+0x24U" in lod
assert "rdthing+0x20U" in lod
assert "source_confirmed" in lod
assert "tests\\sprite_decal_geoset.cpp" in workflow
print("Moving SPR GeoSet/LOD integration checks passed")
