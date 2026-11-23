"""CaptureEnvMap: on-demand six-view equirectangular global image wiring."""
import json
from pathlib import Path
root=Path(__file__).resolve().parents[1]
read=lambda p:(root/p).read_text(encoding="utf-8")
sample=json.loads(read("assets/jkgm.json"))
assert sample["envmap_capture_face_resolution"]==128
assert 'int envmap_capture_face_resolution = 128;' in read("common/config.hpp")
assert '"envmap_capture_face_resolution"' in read("common/config.cpp")
main=read("renderer/main.cpp")
renderer=read("renderer/renderer.cpp")
capture=read("renderer/hemi_reflection_capture.inc")
converter=read("renderer/global_env_equirect_converter.hpp")
registry=read("renderer/global_env_capture.hpp")
assert 'void __cdecl cog_capture_env_map(void *ctx)' in main
assert 'cog_verb_entry{"captureenvmap", cog_capture_env_map}' in main
assert 'float const fade=cog_pop_float(ctx);' in main
assert 'std::memcpy(position.data(),bytes+0xb8U' in main
assert 'get_global_env_capture_controls().request(' in main
assert 'get_global_env_capture_controls().clear();' in main
assert 'global_env_capture_request' in registry
assert 'prepare_world' in registry and 'std::mutex' in registry
assert 'global_env_current=std::move(output);' in capture
assert 'fade_from=std::move(global_env_current);' in capture
assert 'fade_from=std::move(global_env_previous);' in capture
assert 'global_env_previous=std::move(fade_from);' in capture
assert 'global_env_fade_seconds=global_request->fade_time;' in capture
assert 'global_env_fade_start=published_at;' in capture
assert 'choose_global_env_fade_origin(' in capture
assert 'global_env_converter->rebase(' in capture
assert 'if(!rebase_ok)' in capture
assert capture.index('if(!rebase_ok)')<capture.index('global_env_previous=std::move(fade_from);')
assert 'global_env_completed_sequence=global_request->sequence;' in capture
assert 'global_env_fade_factor(' in renderer
assert 'global_env_fade_factor(' in registry
# Old texture GL handles must remain valid for previously staged material
# groups until the next frame's material cache has been cleared.
assert 'global_env_retired' in renderer
assert 'global_env_retired.clear();' in renderer
assert capture.count('global_env_retired.push_back(')>=4
present=renderer.split('void present_game() override',1)[1]
assert present.index('gpu_frame_material_cache.clear();') < (
    present.index('world_frame_material_cache.clear();')) < (
    present.index('global_env_retired.clear();'))
assert 'global_env_fade_origin::composited_capture' in capture
assert 'vec4(mix(a,b,old_mix),1.0)' in converter
assert 'glGenerateMipmap(GL_TEXTURE_2D)' in converter
assert 'envmap_capture_face_resolution' in capture
assert 'global_env_converter->convert(' in capture
assert 'candidate.is_global' in capture
# A global capture is intentionally a Cog snapshot, not a shadow-static pass.
assert 'recently_moving_owners' not in capture
assert 'type!=7U' in registry
assert '(move_type!=0U && move_type!=2U)' in registry
assert '(flags&(0x2U|0x10U|0x20U|0x80000U))' in registry
assert '(flags&(0x2U|0x10U|0x80000U))' in read("renderer/planar_reflection_things.hpp")
# Offscreen native pose acquisition must not discard LEVELGEO after discovery.
native=read("renderer/main.cpp")
pos=native.index("std::vector<planar_reflection_native_node> native_nodes;")
assert pos>=0
offscreen=native[pos:native.index("collect_planar_reflection_native_nodes(",pos)]
assert "(flags&(0x2U|0x10U|0x80000U))" in offscreen
assert "(flags&(0x2U|0x8U|0x10U|0x80000U))" not in offscreen
assert 'glClearColor(candidate.is_global?' in capture
assert 'generated_global_env_binding() const' in renderer
assert 'global_allowed &&' in renderer
assert 'generated_env.active' in renderer
assert 'generated_env.transition' in renderer
assert 'global_env_previous.reset();' in renderer
assert 'source.layers()!=6' in converter
assert '4*side,2*side' in converter
assert 'GL_TEXTURE_WRAP_S,GL_REPEAT' in converter
assert 'glGenerateMipmap(GL_TEXTURE_2D)' in converter
assert 'glDrawArrays(GL_TRIANGLES,0,3)' in converter
assert 'sample_face(direction_for(' in converter
assert 'draw_reflection_capture_sky(shader,camera,view,frustum,' in capture
assert 'capture_sky_mode' in read('assets/shaders/planar_capture.frag')
assert 'global_env_equirect_converter.hpp' in read('renderer/renderer.vcxproj')
assert 'global_env_capture.hpp' in read('renderer/renderer.vcxproj.filters')
print("CaptureEnvMap COG, independent capture, equirect, and fade contracts OK")
