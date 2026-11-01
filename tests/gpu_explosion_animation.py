"""Prevent regressions of per-Thing GPU-cached explosion cel selection.

JK's sithExplosion_CreateThing initializes an animation from num_texinfo
and the original Thing 'timer'. It advances rdThing.wallCel natively;
when it remains zero, the GPU renderer uses the existing remaining
lifetime, not a second renderer timebase.
"""
from pathlib import Path

root = Path(__file__).resolve().parents[1]
main = (root / "renderer/main.cpp").read_text(encoding="utf-8")
helper = (root / "renderer/sprite_explosion_animation.hpp").read_text(encoding="utf-8")
quad = (root / "renderer/sprite_gpu_quad.hpp").read_text(encoding="utf-8")
renderer = (root / "renderer/renderer.cpp").read_text(encoding="utf-8")

assert "initial_ms-uint32_t(remaining_ms)" in helper
assert "initial_ms/cels" in helper
assert "out=native_cel>0 ?native_cel" in helper
assert "read(uintptr_t(owner)+0x134U,&initial_ms" in helper
assert "read(uintptr_t(owner)+0x18U,&remaining_ms" in helper
assert "read(material+0x64U,&cels" in helper
assert "native_explosion_sprite_cel(" in main
assert "resource.material,cel,selected" in main
assert "variant->second.geometry" in main
assert "int32_t requested_animated_cel=-1;" in main
assert "*wall_cel=requested_animated_cel;" in main
assert "return profile_native_sprite_draw(thing,orientation);" in main
assert "restore{wall_cel,saved}" in main
assert "SPR animated explosion native parity" not in main
assert "return profile_native_sprite_draw(thing,orientation);" not in main.split(
    "if(jkgm::native_animated_explosion_sprite(", 1)[1].split(
    "auto resource_item=gpu_sprite_quads.find(sprite)", 1)[0]
# Full GPU path still resolves textures per material AND explicit cel.
assert "group.material=mat;group.cel=cel;" in quad
assert "uint32_t(group.cel)" in renderer
assert "gpu_mesh_materials_ready(*variant->second.geometry)" in main
print("GPU explosion sprite frame selection and texture warm-up wiring OK")
