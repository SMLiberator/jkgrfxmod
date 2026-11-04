"""Protect native rdModel3_DrawMesh fallback from malformed face vertex IDs.

The patched JK executable uses address 0x446360 for the native mesh
function, with an unchecked face->pos_indices[0] back-face probe near
0x446758. Corrupt 3DOs can be rejected from GPU caching and then crash
in this fallback. The guard does not alter valid native mesh draws.
"""
from pathlib import Path
root=Path(__file__).resolve().parents[1]
src=(root/"renderer/main.cpp").read_text(encoding="utf-8")

assert "constexpr uintptr_t native_mesh_addr = 0x00446360U;" in src
assert "constexpr uintptr_t native_face_addr = 0x004467D0U;" in src
assert "bool native_fallback_bad_first_vertex(" in src
assert "gpu_span_readable(m.faces," in src
assert "gpu_span_readable(f.pos_indices,sizeof(first))" in src
assert "std::memcpy(&first,f.pos_indices,sizeof(first));" in src
assert "if(readable && first>=0 && first<m.num_vertices)continue;" in src
assert 'reason="<<(readable?"out_of_bounds":"unreadable_index")' in src

fallback=src[src.index("void __cdecl HookedNativeMesh("):
             src.index("int __cdecl HookedNativeFace(")]
assert fallback.index("bool const queued=jkgm::queue_gpu_3do_mesh(") < (
    fallback.index("native_fallback_bad_first_vertex(mesh,invalid_first_vertex)"))
assert fallback.index("native_fallback_bad_first_vertex(mesh,invalid_first_vertex)") < (
    fallback.index("RealNativeMesh(mesh,placement);"))
assert "Native rdModel3_DrawMesh fallback skipped" in fallback
assert "static std::unordered_set<std::string> reported;" in fallback

# Register correlation with Wine crash: this scale-4 index is three times
# the vertex index (rdVector3 = 3 floats), plus 4 for the Y coordinate.
eax=0x0CC60CF0
edx=0x0D718A60
assert eax%3==0
assert eax//3==0x04420450
assert edx+eax*4+4==0x4089BE24
assert eax//3>65536

print("Native cached-3DO fallback corrupt-index guard checked")
