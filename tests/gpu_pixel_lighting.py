"""Source-level checks for opt-in GPU-cached 3DO pixel diffuse lighting.

This is not a GLSL compiler or an in-game visual regression. It validates
stage interfaces and the explicit no-double-lighting switch. The optional
GLSL compilation/link check runs when glslangValidator is installed.
"""
from pathlib import Path
import re
import shutil
import subprocess

root = Path(__file__).resolve().parents[1]
vert_path = root / "assets/shaders/gpu_3do_preview.vert"
frag_path = root / "assets/shaders/gpu_3do_preview.frag"
vert = vert_path.read_text(encoding="utf-8")
frag = frag_path.read_text(encoding="utf-8")
config = (root / "common/config.hpp").read_text(encoding="utf-8")
parser = (root / "common/config.cpp").read_text(encoding="utf-8")
wiring = (root / "renderer/renderer.cpp").read_text(encoding="utf-8")
program = (root / "renderer/shaders.cpp").read_text(encoding="utf-8")

required_varyings = {
    "pixel_light_face_anchor": "vec3",
    "pixel_light_face_normal": "vec3",
    "pixel_light_vertex_rgb": "vec3",
    "pixel_light_state": "vec3",
    "local_fragment_position": "vec3",
    "local_fragment_normal": "vec3",
}
for name, kind in required_varyings.items():
    assert re.search(r"\bout\s+" + kind + r"\s+" + name + r"\s*;", vert), name
    assert re.search(r"\bin\s+" + kind + r"\s+" + name + r"\s*;", frag), name

assert "bool gpu_pixel_lighting = false" in config
assert 'j.contains("gpu_pixel_lighting")' in parser
assert "the_config->gpu_pixel_lighting" in wiring
assert "instance.num_lights" in program
assert 'gl::set_uniform_integer(pixel_dynamic_lighting,pixel_lighting?1:0)' in program
assert "camera_hud_thing!=0 || pixel_dynamic_lighting!=0" in vert
assert "jk_lit_rgb=vec3(1.0);" in vert
assert "pixel_dynamic_lighting!=0" in frag
assert "pixel_jk_diffuse_lighting()" in frag
assert "jk_fragment_lit" in frag
assert "jk_lit_rgb*(1.0-env_metalness)" not in frag
assert "albedo.rgb*jk_fragment_lit" in frag
assert "dot(jk_fragment_lit,vec3(0.2126" in frag
assert "nonrigid_light_transform!=0" in frag
assert "light_position_radius[i].xyz-anchor" in frag

compiler = shutil.which("glslangValidator")
if compiler:
    # Both shader stages are linked, so mismatched varyings are caught too.
    subprocess.run([compiler, "-l", str(vert_path), str(frag_path)],
                   check=True, cwd=root)
print("GPU pixel lighting shader source/wiring checks passed")
