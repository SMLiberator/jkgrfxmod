"""Guard unified GPU caching, permanent optimizations, and opt-in MAT warming."""
from pathlib import Path
import re
root=Path(__file__).resolve().parents[1]
read=lambda p:(root/p).read_text(encoding="utf-8")
header=read("common/config.hpp")
config=read("common/config.cpp")
main=read("renderer/main.cpp")
renderer=read("renderer/renderer.cpp")
assert "bool use_gpu_cache = true;" in header
assert "bool enable_gpu_mat_warmup = false;" in header
assert 'j.at("use_gpu_cache").get_to(rv->use_gpu_cache);' in config
assert "bool const revision_fastpath=mesh_revision::active();" in main
assert "TrueJkInitNewCogVerbs(mesh_revision::register_mutator,ctx)" in main
assert "hud_slab_valid[hud_back_index][curr_slab]=true;" in renderer
assert "std::memcpy(mirror.data()+offset," in renderer
for key in ("use_model_cache","use_sprite_cache","world_cache_preview",
            "mesh_revision_tracking","optimize_hud_uploads"):
    assert not re.search(r"\b"+re.escape(key)+r"\b", header)
    assert not re.search(r"\b"+re.escape(key)+r"\b", main)
    assert not re.search(r"\b"+re.escape(key)+r"\b", renderer)
    assert f'j.at("{key}").get_to(' not in config
    assert f'"{key}"' in config # warning only
assert "the_config->enable_gpu_mat_warmup" in main
assert "the_config->enable_gpu_mat_warmup" in renderer
assert "the_config->use_gpu_cache" in main
assert "the_config->use_gpu_cache" in renderer
assert "enable_native_fallback" in renderer
print("Unified GPU caching and permanent optimization integration checks passed")
