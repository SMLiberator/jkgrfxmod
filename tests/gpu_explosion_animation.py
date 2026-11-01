"""SPR GPU cel layout verified against user's patched jkgm(5).exe.

At RVA 0x6c359, native rdSprite_Draw reads [rdThing+0x2c] and stores
it in [rdProcEntry+0x30]. The two offsets must never be conflated.
Explosions use native sithSurface animation timing and Thing wallCel.
"""
from pathlib import Path
root=Path(__file__).resolve().parent.parent
read=lambda f:(root/f).read_text(encoding="utf-8")
main=read("renderer/main.cpp")
helper=read("renderer/native_sprite_cel.hpp")
quad=read("renderer/sprite_gpu_quad.hpp")
renderer=read("renderer/renderer.cpp")
assert "constexpr uintptr_t native_sprite_wall_cel_offset=0x2cU" in helper
assert "read_native_sprite_wall_cel(" in main
assert "sprite_native_cel_copy_addr=0x0046D359U" in main
assert "0x8b,0x51,0x2c,0x8b,0x4f,0x60,0x89,0x56,0x30" in main
assert "cel<-1 || value>255" not in main  # range checked in helper
assert "value<-1 || value>255" in helper
assert "native_explosion_sprite_cel(" not in main
assert "requested_animated_cel" not in main
assert "sprite_explosion_animation.hpp" not in main
assert "group.material=mat;group.cel=cel;" in quad
assert "uint32_t(group.cel)" in renderer
assert "gpu_mesh_materials_ready(" in main
assert "SPR GPU MAT unavailable " in main
assert "SPR GPU reject stage=" in main
assert "SPR GPU unavailable: authored SPR type not registered" in main
print("Patched JKGM sprite cel and GPU fallback diagnostic contracts passed")
