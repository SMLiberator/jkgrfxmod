"""Experimental 3DO sector-crossing lighting must stay opt-in and bounded.

Source checks complement the mock-native C++ selection tests. Compile/link
validation happens at runtime and is covered by optional GLSL validation.
"""
from pathlib import Path
root=Path(__file__).resolve().parents[1]
read=lambda p:(root/p).read_text(encoding="utf-8")
config=read("common/config.hpp")
parser=read("common/config.cpp")
render=read("renderer/renderer.cpp")
main=read("renderer/main.cpp")
instance=read("renderer/gpu_3do_preview.hpp")
select=read("renderer/sector_crossing.hpp")
staging=read("renderer/sector_crossing_gpu.hpp")
vertex=read("assets/shaders/gpu_3do_preview.vert")
fragment=read("assets/shaders/gpu_3do_preview.frag")
api=read("renderer/shaders.cpp")
assert "bool enable_sector_crossing_lighting = false;" in config
assert "bool experimental_sector_crossing_vertex = false;" in config
assert 'j.contains("experimental_sector_crossing_vertex")' in parser
assert 'j.contains("enable_sector_crossing_lighting")' in parser
assert "if(the_config->enable_sector_crossing_lighting ||" in main
assert "active_native_model_sector_id_valid" in main
assert "inst.owning_sector_id=active_native_model_sector_id;" in main
assert "sector_crossing_collect_candidates(" in staging
assert "sector_crossing_instance_sphere(" in staging
assert "if(choices.overflow || choices.count<2" in staging
assert "sector_crossing_gpu_max_planes=96" in staging
assert "static constexpr uint32_t capacity=4;" in select
assert "sector_crossing_plane_texture" in render
assert "glTexBuffer(GL_TEXTURE_BUFFER,GL_RGBA32F,buffer)" in render
assert "sector_crossing_stage_instance(" in render
assert "stage_sector_crossing();" in render
assert "reset_sector_crossing();" in render
assert "prog.set_sector_crossing(inst.crossing_lighting.get()," in render
assert "prog.set_sector_crossing(nullptr,false,false,false);" in render
assert "if(!the_config->enable_sector_crossing_lighting)" in render
# The condition now also excludes skyboxes and projected sprites/decals.
assert "crossing_buffer_ready && allow_sector_effects &&" in render
assert "!inst.camera_hud && !inst.is_projected_decal &&" in render
assert "!inst.is_sprite_quad" in render
assert "sector_crossing_planes" in api
assert "set_sector_crossing(" in api
setter=api[api.index("gpu_3do_preview_shader::set_sector_crossing("):]
# Regression: samplerBuffer on unit 12 collided with spotlight cookie
# sampler2D (units 12..15), silently suppressing all cached 3DO draws.
# All active sampler types must occupy different numeric texture units
# even when the fragment's sector-crossing branch is disabled.
import re
unit_match=re.search(
    r"constexpr int gpu_3do_sector_plane_texture_unit\s*=\s*(\d+);",
    instance)
assert unit_match, "sector sampler unit must be explicitly reserved"
plane_unit=int(unit_match.group(1))
assert plane_unit==19
spotlight_cookie_units={12+slot for slot in range(4)}
translucent_sun_units={16,17,18}
assert plane_unit not in spotlight_cookie_units | translucent_sun_units
assert "gl::set_uniform_integer(translucent_spotlight_images[slot],12+slot);" in api
assert (
    "gl::set_uniform_integer(\n"
    "        sector_crossing_planes,gpu_3do_sector_plane_texture_unit);"
) in setter[:setter.index("if(!count)return;")]
assert render.count("gpu_3do_sector_plane_texture_unit")>=3
assert render.count("glActiveTexture(GL_TEXTURE0+\n")>=2
assert "fragment_units<=gpu_3do_sector_plane_texture_unit" in render
assert "glGetIntegerv(GL_MAX_TEXTURE_IMAGE_UNITS,&fragment_units);" in render
assert "prog.set_sector_crossing(nullptr,false,false,false);" in render
assert "sector_crossing_lit[4]" in vertex
assert "sector_crossing_lit[4]" in fragment
assert "if(sector_crossing_count>1)" in vertex
assert "if(i==0) {sector_crossing_lit[i]=jk_lit_rgb;continue;}" in vertex
assert "select_sector_crossing_slot()" in fragment
assert "select_sector_crossing_vertex(vec3 view_position)" in vertex
assert "texelFetch(sector_crossing_planes,begin_plane+j)" in vertex
assert "sector_crossing_per_vertex!=0 && i!=selected_slot" in vertex
assert "jk_lit_rgb=sector_crossing_lit[selected_slot]" in vertex
assert "sector_vertex_cmp=sector_crossing_cmp[selected_slot].xyz" in vertex
assert "sector_vertex_tint=sector_crossing_tint[selected_slot].xyz" in vertex
assert "in vec3 sector_vertex_cmp;" in fragment
assert "in vec3 sector_vertex_tint;" in fragment
assert "sector_crossing_per_vertex==0" in fragment
assert "experimental_sector_crossing_vertex" in render
assert "gl::set_uniform_integer(sector_crossing_per_vertex," in setter
assert "gl::set_uniform_integer(sector_crossing_count,count);" in setter
assert "count>1 && per_vertex ? 1 : 0" in setter
assert "texelFetch(sector_crossing_planes,begin_plane+j)" in fragment
assert "sector_crossing_count>1" in fragment
assert "pixel_jk_diffuse_lighting(diffuse_local_normal)" in fragment
assert "if(all(lessThanEqual(added,vec3(0.0)))) return baked;" in fragment
assert "gpu_material_palette_fullbright(" in fragment
assert "world_lighting_exempt" in fragment
assert "can_replace_sector_tint" in render
# This first iteration never applies crossing to projected decals or HUDs.
assert "instance.is_projected_decal" in staging
assert "instance.is_sprite_quad" in staging
assert "instance.camera_hud" in staging
# Ensure no accidental O(total_sectors) scan in each render frame.
assert "index_by_id.find(owner_id)" in select
assert "for(uint32_t neighbor_index:current.adjoining)" in select
assert "constexpr size_t visit_limit=64;" in select
assert "visited[discovered++]" in select
print("Sector-crossing opt-in / GPU 3DO shader integration checks passed")
