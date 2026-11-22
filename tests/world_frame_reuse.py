"""Guard main-camera cached-world MAT/light reuse without changing alpha order.

The behavior-level world_cache.cpp tests still cover native policy choices;
this check protects cross-pass lifetime and sorting wiring.
"""
from pathlib import Path

root=Path(__file__).resolve().parents[1]
render=(root/"renderer/renderer.cpp").read_text(encoding="utf-8")
alpha_h=(root/"renderer/world_cache_alpha.hpp").read_text(encoding="utf-8")
assert "choose_world_cache_draw_policy(" in alpha_h
assert "world_cache_draws_in_pass(" in alpha_h

# Both kinds of snapshots must be invalidated at presentation and scene reset.
assert render.count("world_frame_material_cache.clear();")==2
assert render.count("world_frame_lights={};")==2
assert "std::map<std::tuple<uintptr_t,int32_t,bool>" in render
assert "world_frame_material_entry const &resolve_world_frame_material(" in render
assert "entry.texture=solid" in render
assert "return world_frame_material_cache.emplace(" in render

lighting=render.split("world_frame_camera_lighting const &snapshot_world_frame_lights()",1)[1]
lighting=lighting.split("world_frame_material_entry const &resolve_world_frame_material(",1)[0]
assert lighting.count("read_world_cache_camera_lights(")==1
assert "if(!frame.sampled)" in lighting
assert "frame.ready=read_world_cache_camera_lights(" in lighting
assert "frame.attenuation" in lighting

opaque=render.split("void draw_world_cache_preview()",1)[1]
opaque=opaque.split("void draw_world_cache_transparency()",1)[0]
alpha=render.split("void draw_world_cache_transparency()",1)[1]
alpha=alpha.split("void draw_game_transparency_pass(",1)[0]

assert "snapshot_world_frame_lights();" in opaque
assert "snapshot_world_frame_lights();" in alpha
assert "read_world_cache_camera_lights(" not in opaque
assert "read_world_cache_camera_lights(" not in alpha
assert "auto const &active_lights=(pixel_lighting || point_specular)" in opaque
assert "auto const &active_lights=(pixel_lighting || point_specular)" in alpha
assert "shader.set_lights(active_lights,light_attenuation," in opaque
assert "shader.set_lights(active_lights,light_attenuation," in alpha
assert "point_lights.clear();" not in opaque
assert "point_lights.clear();" not in alpha

# Sky and grouped opaque surfaces share the same frame-cel resolution.
assert "face.material,face.cel,solid).texture;" in opaque
assert "first->material,first->cel,solid);" in opaque
assert "auto const &info=entry.info;" in opaque
assert "choose_world_cache_draw_policy(*first,info)" in opaque

# Transparent faces retain stable std::map material references and their
# original per-face policies while remaining sorted back-to-front.
assert "world_frame_material_entry const *material=nullptr;" in alpha
assert "draws.push_back({&face,&entry,policy});" in alpha
assert "auto const &material=draw.material->texture;" in alpha
assert "auto const policy=draw.policy;" in alpha
assert "float const da=depth(a.face),db=depth(b.face);" in alpha
assert "if(da==db)return a.face->surface_index<b.face->surface_index;" in alpha
assert "return da>db;" in alpha
assert "gl::set_depth_mask(false);" in alpha
assert "has_fluid=has_fluid ||" in alpha
assert "std::map<std::tuple<" not in alpha

print("World frame MAT, alpha classification and camera-light reuse passed")
