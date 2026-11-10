"""Source-contract checks for opt-in vertex ambient and portal continuity."""
from pathlib import Path

root=Path(__file__).resolve().parents[1]
cfg=(root/"common/config.hpp").read_text(encoding="utf-8")
parser=(root/"common/config.cpp").read_text(encoding="utf-8")
sample=(root/"assets/jkgm.json").read_text(encoding="utf-8")
r=(root/"renderer/renderer.cpp").read_text(encoding="utf-8")
native=(root/"renderer/main.cpp").read_text(encoding="utf-8")
probes=(root/"renderer/world_vertex_ambient.hpp").read_text(encoding="utf-8")

assert "bool experimental_world_vertex_ambient = false" in cfg
assert "float experimental_world_vertex_ambient_radius = 1.0f" in cfg
assert 'j.contains("experimental_world_vertex_ambient")' in parser
assert 'j.contains("experimental_world_vertex_ambient_radius")' in parser
assert '"experimental_world_vertex_ambient": false' in sample
assert '"experimental_world_vertex_ambient_radius": 1.0' in sample
assert "stage_world_cache();\n            stage_world_vertex_ambient();\n            stage_sector_crossing();" in r
assert "void stage_world_vertex_ambient()" in r
assert "capture_world_vertex_ambient(" in r
assert "inst.ambient=sampled;" in r
assert "sample_with_portals(" in r
assert "portal_mixed=" in r
assert "portal_only=" in r
assert "++missed; // Retain native ambient when no probe exists." in r
assert "the_config->enable_sector_crossing_lighting ||\n               the_config->experimental_world_vertex_ambient" in native
assert "inst.owning_sector_id" in r
assert "inst.shadow_only || inst.camera_hud" in r
assert "the_config->enable_sector_crossing_lighting ||" in r
assert "range.surface_flags&0x600U" in probes
assert "range.passable_adjoin" in probes
assert "cell_count() const" in probes
assert "cells_.find(key{sector" in probes
assert "read(face+0x54U,&static_ptr,4U)" in probes
assert "clamp_authored_light(intensity[i],overbright)" in probes
assert "clamp_authored_extra_light(face_extra,overbright)" in probes
assert "clamp_authored_extra_light(sector_extra,overbright)" not in probes
assert "read(uintptr_t(sector)+0x08U,&sector_extra,4U)" not in probes
assert "std::max(0.0f,authored+extra)" in probes
assert "capture_world_vertex_ambient_portals(" in probes
assert "grid.add_portal(a,b,polygon)" in probes
assert "read(uintptr_t(adjoin)+0x04U,&destination,4U)" in probes
assert "triangle_distance2(" in probes
assert "portal_distance2(" in probes
assert "world_vertex_ambient_result::mixed" in probes
assert "world_vertex_ambient_result::portal_only" in probes
assert "glDraw" not in probes and "glBind" not in probes
print("World vertex ambient source contracts OK")
