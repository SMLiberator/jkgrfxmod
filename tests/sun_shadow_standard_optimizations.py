"""The four confirmed sunlight-shadow optimizations are unconditional.

Keep shadow enable switches and quality parameters independent.
"""
from pathlib import Path
import re

root=Path(__file__).resolve().parents[1]
read=lambda p:(root/p).read_text(encoding="utf-8")
header=read("common/config.hpp")
config=read("common/config.cpp")
renderer=read("renderer/renderer.cpp")
docs=read("doc/config.md")
retired=("sun_dynamic_shadow_adaptive_resolution","enable_sun_static_cache",
         "sun_shadow_stable_projection_cache","sun_shadow_instance_batches")
for name in retired:
    assert not re.search(r"\b"+name+r"\b",header)
    assert not re.search(r"\b"+name+r"\b",renderer)
    assert f'j.at("{name}").get_to(' not in config
    assert f'"{name}"' in config  # legacy warning only
    assert f"| **{name}** |" not in docs
assert "sun_shadow_cascade_cached[cascade] &&" in renderer
assert "bool static_same=map.static_complete &&" in renderer
assert "true,map.static_complete);" in renderer
assert "if(map.static_complete)" in renderer
assert "sun_dynamic_shadow_budget_tile(" in renderer
assert "tile_layout.undo_last();" in renderer
assert "prog.set_instanced_models(transforms,amount);" in renderer
assert "glDrawElementsInstanced(" in renderer
assert "enable_sun_dynamic_shadows" in header
assert "sun_shadow_resolution" in header
assert "sun_dynamic_shadow_resolution" in header
print("Standard sunlight shadow optimizations integration checks passed")
