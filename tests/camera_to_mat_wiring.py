"""CameraToMat COG ABI, albedo-only binding, lifetime and capture contracts."""
from pathlib import Path
root=Path(__file__).resolve().parents[1]
read=lambda s:(root/s).read_text(encoding="utf-8")
main=read("renderer/main.cpp")
renderer=read("renderer/renderer.cpp")
capture=read("renderer/camera_to_mat_capture.inc")
controls=read("renderer/camera_to_mat.hpp")
runtime=read("renderer/runtime_material.hpp")
doc=read("doc/offscreen_captures.md")
assert '#include "camera_to_mat.hpp"' in main and '#include "camera_to_mat.hpp"' in renderer
assert 'cog_verb_entry{"cameratomat", cog_camera_to_mat}' in main
assert 'cog_verb_entry{"reloadmaterial", cog_reload_material}' in main
assert 'cog_verb_entry{"resetmaterial", cog_reload_material}' in main
assert "float const fov=cog_pop_float(ctx);" in main
assert "int const mat=cog_pop_int(ctx);" in main
assert "void *thing=cog_pop_thing(ctx);" in main
assert "cog_material_controls().reset_mat(uint32_t(mat));" in main
assert "get_camera_to_mat_controls().erase(uint32_t(mat));" in main
assert "void reset_mat(uint32_t mat)" in runtime
assert "std::map<uint32_t,camera_to_mat_request>" in controls
assert "max_targets=4U" in controls
assert "get_camera_to_mat_controls().prepare_world(state.current_world)" in renderer
assert "get_camera_to_mat_controls().clear();" in renderer
assert "camera_to_mat_targets.clear();" in renderer
assert "render_camera_to_mat_captures();" in renderer
assert renderer.index("render_camera_to_mat_captures();") < renderer.index("render_planar_capture();")
assert "camera_to_mat_albedo(native_id)" in renderer
assert "out.albedo_source=1;" in renderer
assert "out.has_image=true;" in renderer
assert "out.texture=*live_camera_albedo;" in renderer
assert "mat->emissive_map.has_value()" in renderer
assert "prog->set_albedo_factor(material_color)" in renderer
assert "out.emission_factor=" in renderer
assert "out.reflection_strength=" in renderer
assert "out.clearcoat_params=" in renderer
assert "camera_to_mat_drawing=true" in capture
assert "camera_to_mat_drawing=false" in capture
assert "camera_to_mat_albedo(uint32_t mat) const" in renderer
assert "if(camera_to_mat_drawing)return std::nullopt;" in renderer
assert "signature!=uint32_t(request.owner)" in capture
assert "(flags&0x2U)!=0U" in capture
assert "kind<1U || kind>15U" in capture
assert "portals.traverse(" in capture
assert "portals.surface_in_window(" in capture
assert "visible_thing_sectors" in capture
assert "visible_surfaces" in capture
assert "request.world!=world" in capture
assert "surface->desc.dwWidth" in capture and "surface->desc.dwHeight" in capture
assert "make_offscreen_capture_projection(" in capture
assert "camera.fov=request.fov;" in capture
assert "projection.orthographic?nullptr:&frustum" in capture
assert "draw_reflection_capture_sky(" in capture
assert "collect_planar_reflection_things(" in capture
assert "acquire_planar_reflection_offscreen_gpu(" in capture
assert "reflection_world_buckets.buckets()" in capture
assert "glGenerateMipmap(GL_TEXTURE_2D)" in capture
assert "ReloadMaterial(targetMat)" in doc
assert 'Include="camera_to_mat.hpp"' in read("renderer/renderer.vcxproj")
assert 'Include="camera_to_mat.hpp"' in read("renderer/renderer.vcxproj.filters")
print("CameraToMat albedo-only capture, COG and lifetime contracts OK")
