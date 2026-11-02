"""Source invariants for native JK MAT cel and surface UV animation.

JK owns frame timing and animation IDs. GPU caches consume current cels:
- face -1 => rdMaterial.current_cel;
- ordinary sprites => each rdThing.wallCel;
- world faces => current SithSurface.wallCel and texVertOffset.
"""
from pathlib import Path

root=Path(__file__).resolve().parent.parent
read=lambda path:(root/path).read_text(encoding="utf-8")
renderer=read("renderer/renderer.cpp")
native=read("renderer/main.cpp")
world=read("renderer/world_cache_visibility.hpp")
world_gpu=read("renderer/world_cache_gpu.hpp")
sprite=read("renderer/sprite_gpu_quad.hpp")
scroll=read("renderer/world_cache_uv_animation.hpp")
vertex=read("assets/shaders/world_cache_wireframe.vert")
fragment=read("assets/shaders/world_cache_texture_preview.frag")

# Native material cel can change without invalidating mesh geometry.
assert "group.cel<0?static_cast<int>(mat.current_cel):group.cel" in renderer
assert "int32_t cel = group.cel < 0" in renderer
assert "static_cast<int32_t>(mat.current_cel) : group.cel;" in renderer
assert "gpu_mat_mesh_needs_recheck(" in native
assert "ref.second<0" in native and "mat.current_cel" in native
assert "int32_t const cel=static_cast<int32_t>(face.cel)" in native

# Animated explosion sprites can share one SPR and show distinct cel frames.
assert "read_native_sprite_wall_cel(" in native
assert "native_sprite_cel.hpp" in native
assert "std::unordered_map<int32_t,cached_gpu_sprite_quad>" in native
assert "group.material=mat;group.cel=cel;" in sprite

# World staged surface metadata is live, including native UV scroll.
# The live read now starts at SithSurface +0x0c (adjoin pointer),
# rather than +0x10. Original absolute native offsets are unchanged:
# wallCel +0x38, texVertOffset +0x3c, and adjoin +0x0c.
assert "std::array<uint8_t,0x38> raw{};" in world
assert "read(uintptr_t(addr)+0x0cU,raw.data(),raw.size())" in world
assert "std::memcpy(&adjoin,raw.data(),4);" in world
assert "std::memcpy(&cel,raw.data()+0x2c,4);" in world
assert "std::memcpy(uv_offset.data(),raw.data()+0x30,8);" in world
assert "face_type,false,uv_offset" in world
assert "write_world_cache_uv_animation(" in renderer
assert "frame_uv_offset.assign(cache.mesh.vertices.size()" in renderer
assert "offsets[i]=candidate.uv_offset;" in scroll
assert "gl::enable_vertex_attrib_array(6);" in world_gpu
assert "layout(location=6) in vec2 native_uv_offset;" in vertex
assert "material_uv=native_uv+native_uv_offset;" in vertex
# World texture UVs remain unchanged for ordinary faces. Sky mode replaces
# the source UV before the same MAT-pixel scaling and material scroll.
assert "vec2 uv=material_uv;" in fragment
assert "gpu_material_sample_albedo(" in fragment
assert "material_texcoord=uv*uv_scale;" in fragment
assert "uv+=sky_offset+sky_face_offset;" in fragment
assert "glMultiDrawElements(" in renderer
print("JK native cel and cached-world UV animation checks passed")
