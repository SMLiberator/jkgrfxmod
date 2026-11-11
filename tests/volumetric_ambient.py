"""New CPU-only 2x2x2 volumetric ambient field: Batch A contracts.

The production renderer is deliberately NOT modified until its sampled
output and quantization semantics are validated. Batch B will wire this
to GPU per-vertex interpolation and dynamic sector ExtraLight floors.
"""
from pathlib import Path
root=Path(__file__).resolve().parents[1]
header=(root/"renderer/volumetric_ambient.hpp").read_text(encoding="utf-8")
tests=(root/"tests/volumetric_ambient.cpp").read_text(encoding="utf-8")
wiring=(root/"renderer/renderer.cpp").read_text(encoding="utf-8")
assert "struct volumetric_ambient_sector {" in header
assert "std::array<uint8_t,8> light{};" in header
assert "float range=1.0f;" in header
assert "float const range=authored_light_limit(overbright);" in header
assert "for(unsigned corner=0;corner<8;++corner)" in header
assert "brightness+=weight*(float(light[corner])/255.0f)*range;" in header
assert "std::clamp((position[d]-lo[d])/span,0.0f,1.0f)" in header
assert "std::lround(" in header
assert "std::sort(sectors_.begin(),sectors_.end()" in header
assert "building_.clear(); // Startup-only source samples" in header
assert "bool capture_volumetric_ambient(" in header
assert "mesh.vertices[first+i].position" in header
assert "clamp_authored_light(intensity,overbright)" in header
assert "range.surface_flags&0x600U" in header
assert "range.passable_adjoin" in header
assert "geo<3U || mode<3U" in header
# The new field must capture ONLY the authored Gouraud vertices:
# sector AmbientLight is Thing-only and dynamic/face ExtraLight must not
# become a permanent part of the startup volume.
assert "read(face+0x44U" not in header
assert "read(sector+0x08U" not in header
assert "read(uintptr_t(sector)+0x08U" not in header
assert "read(sector+0x04U" not in header
assert "capture_world_vertex_ambient(" not in header
assert '#include "volumetric_ambient.hpp"' not in wiring
assert "experimental_world_vertex_ambient" in wiring  # kept until Batch B
assert "mem.put(addr+0x44U,1.5f)" in tests
assert "assert(field.sectors()[0].range==1.0f);" in tests
assert "assert(field.sectors()[0].range==2.0f);" in tests
assert "assert(field.sectors()[0].id==7U && field.sectors()[1].id==8U);" in tests
assert "assert(!field.sample(9," in tests
assert "field.sample(7,{{0.25f,0.0f,0.0f}}" in tests
assert "stats.surfaces_seen==3U && stats.surfaces_sampled==2U" in tests
print("Volumetric ambient Batch A source contracts passed")
