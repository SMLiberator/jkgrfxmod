"""Shared realtime cubemap integration and removal of matcap reflection mode."""
from pathlib import Path
root=Path(__file__).resolve().parents[1]
read=lambda path:(root/path).read_text(encoding="utf-8")
meta=read("common/material_map.cpp")
mat=read("common/material.hpp")
renderer=read("renderer/renderer.cpp")
capture=read("renderer/hemi_reflection_capture.inc")
world=read("assets/shaders/world_cache.frag")
model=read("assets/shaders/gpu_3do_preview.frag")
shader_h=read("renderer/shaders.hpp")
shader_cpp=read("renderer/shaders.cpp")
samplers=read("renderer/texture_units.hpp")
assert 'v == "shared"' in meta
assert 'v == "matcap"' not in meta
assert "shared=7" in mat and "planar=4, hemi=5, cube=6" in mat
assert "matcap_map" in meta
assert 'shared_reflection_owner_key=~uint64_t(0)' in renderer
assert 'bool shared_reflection_ready_this_frame() const' in renderer
assert 'shared_requested=wants_shared(cache.sector_candidates) ||' in capture
assert 'wants_shared(cache.sector_special_candidates)' in capture
assert 'shared.owner=shared_reflection_owner_key;' in capture
assert 'shared.position=viewer.position;' in capture
assert 'shared.mode=7;' in capture
assert 'shared.is_shared=true;' in capture
assert "reflection_probe_capture_distance_limit(" in capture
assert "selected_mode,candidate.is_global,candidate.is_shared," in capture
assert 'active_owners.insert(shared.owner);' in capture
assert 'candidate.is_shared?0U:candidate.owner' in capture
assert '!candidate.is_global && !candidate.is_shared &&' in capture
assert 'capture_static_owner(owner)' in capture  # Cog-only global capture survives
assert 'the_config->reflection_capture_update_interval' in capture
assert 'int const face_count=paraboloid_mode?2:6;' in capture
assert 'effective_capture_reflection_mode(material.reflection_mode)==7' in renderer
assert 'reflection_probes.find(shared_reflection_owner_key)' in renderer
assert 'bind_world_shared_reflection(shader);' in renderer
assert renderer.count('bind_world_shared_reflection(shader);')==2
assert 'void set_shared_reflection(bool ready,bool hemisphere)' in shader_h
assert 'set_shared_reflection(bool ready)' in shader_cpp
assert '"shared_reflection_map",23,sampler_kind::texture_array' in samplers
assert '#ifdef JKGM_REFLECTION_PROBE' in world
assert 'uniform sampler2DArray shared_reflection_map;' in world
assert 'has_shared=mode==7 && shared_reflection_ready!=0;' in world
assert 'coverage=textureLod(shared_reflection_map,' in world
assert 'mode==6 || mode==7' in model
assert 'has_hemi=(mode==5 || mode==6 || mode==7)' in model
assert 'vec2 env_uv=mode==3' not in world
assert 'vec2 env_uv=mode==3' not in model
assert 'max(max(planar.a,screen.a),has_shared?1.0:0.0)' in world
assert 'candidate.is_global &&' in capture
assert 'envmap' in read("doc/offscreen_captures.md")
print("Shared mode world/3DO captures, fallback, and matcap-mode retirement OK")
