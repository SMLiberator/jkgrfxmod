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
assert 'global_env_previous=std::move(global_env_current);' in capture
assert 'global_env_fade_seconds=global_request->fade_time;' in capture
assert 'envmap_capture_face_resolution' in capture
assert 'global_env_converter->convert(' in capture
assert 'candidate.is_global' in capture
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
assert 'global_env_equirect_converter.hpp' in read('renderer/renderer.vcxproj')
assert 'global_env_capture.hpp' in read('renderer/renderer.vcxproj.filters')
print("CaptureEnvMap COG, independent capture, equirect, and fade contracts OK")
