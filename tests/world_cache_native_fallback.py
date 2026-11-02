"""Prevent world-cache isolation from swallowing native Thing primitives."""
from pathlib import Path
r=Path(__file__).resolve().parents[1]
renderer=(r/"renderer/renderer.cpp").read_text(encoding="utf-8")
assert '#include "world_cache_native_fallback.hpp"' in renderer
assert "void refresh_world_cache_native_fallback()" in renderer
assert "native_fallback.allows(" in renderer
assert "native_world_filter::nonworld" in renderer
assert "world_texture_handles.insert(" in renderer
# World opaque and transparent batches must each be reached with a
# nonworld material filter when isolation is on, not skipped wholesale.
assert "batch_blend_filter::special,world_filter" in renderer
assert "batch_blend_filter::all,world_filter" in renderer
assert renderer.count("batch_blend_filter::regular,world_filter") >= 2
assert renderer.count("?native_world_filter::nonworld:native_world_filter::all;") == 2
assert "refresh_world_cache_native_fallback();\n            animation_seconds =" in renderer
print("world native particle/saber fallback routing checks passed")
