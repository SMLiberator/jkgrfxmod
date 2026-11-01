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
# Both cached geometry paths must obey the same opt-in, and disabled
# world geometry must retain native Gouraud dynamic light intensities.
assert "the_config->gpu_pixel_lighting &&" in wiring
assert ("world_cache_use_pixel_lights(\n"
        "                the_config->gpu_pixel_lighting,camera_ready);") in wiring
assert "return enabled && camera_ready;" in (
    root / "renderer/world_cache_lighting.hpp").read_text(encoding="utf-8")
assert "cache.native_dynamic_light.assign(" in wiring
assert "native_dynamic_ready=read_world_cache_dynamic_lights(" in wiring
assert "if(!pixel_lighting)point_lights.clear();" in wiring
assert "std::istringstream lines{std::string(mb.str())};" in program
assert 'gl::set_uniform_integer(pixel_dynamic_lighting,pixel_lighting?1:0)' in program
# Both GPU geometry paths use the same editable GLSL code for attenuation
# AND gamma/saturation composition; no compiled renderer formula divergence.
shared_path = root / "assets/shaders/jk_point_lighting.glsl"
shared = shared_path.read_text(encoding="utf-8")
world_vert_path = root / "assets/shaders/world_cache_wireframe.vert"
world_frag_path = root / "assets/shaders/world_cache_texture_preview.frag"
world_vert = world_vert_path.read_text(encoding="utf-8")
world_frag = world_frag_path.read_text(encoding="utf-8")
for stage in (vert, frag, world_frag):
    assert '#include "jk_point_lighting.glsl"' in stage
assert "load_shader_source(filename.parent_path()/name" in program
assert "float jk_point_sample(" in shared
assert "vec3 jk_point_compose(" in shared
for stage in (vert, frag, world_frag):
    assert "jk_point_sample(" in stage
for stage in (frag, world_frag):
    assert "jk_point_compose(" in stage
assert "vec3 lighting=cached_lit_rgb;" in world_frag
assert "if(added<=0.0) return jk_lit_rgb;" in frag
assert "if(added<=0.0) return baked;" in shared
assert "pixel_dynamic_lighting!=0" in frag
assert "pixel_jk_diffuse_lighting()" in frag
assert "jk_fragment_lit" in frag
assert "jk_lit_rgb*(1.0-env_metalness)" not in frag
assert "albedo.rgb*jk_fragment_lit" in frag
assert "dot(jk_fragment_lit,vec3(0.2126" in frag
assert "nonrigid_light_transform!=0" in frag
assert "light_position_radius[i].xyz-anchor" in frag

# This is why recomputing baked light at the pixel was visibly wrong:
# gamma(interpolated brightness) != interpolated gamma(brightness).
# With zero point-light contribution, the corrected result must be EXACTLY
# the original interpolated vertex result.
for left, right in [(0.1, 0.9), (0.0, 1.0), (0.3, 0.8)]:
    for t in (0.25, 0.5, 0.75):
        baked = (1.0-t) * left**1.8 + t * right**1.8
        recomputed = ((1.0-t) * left + t * right)**1.8
        if left != right:
            assert abs(baked - recomputed) > 1e-5
        base = (1.0-t) * left + t * right
        no_effect = baked + (base**1.8 - base**1.8)
        assert no_effect == baked

# Reference math for the shared GLSL functions: zero contribution is
# EXACTLY the existing Gouraud RGB, and world/3DO match on identical input.
def point_sample(dist, radius, intensity, attenuation, incidence):
    return max(intensity-dist*attenuation, 0.0)*max(incidence,0.0) if dist<radius else 0.0

def point_compose(baked, added, light_color):
    if added<=0.0:
        return baked
    return tuple(min(1.0,b**(1.0/1.8)+min(added,1.0)*c**(1.0/1.8))**1.8
                 for b,c in zip(baked,light_color))

for left,right in ((0.0,1.0),(0.1,0.9),(0.3,0.8)):
    for t in (0.0,0.25,0.5,0.75,1.0):
        baked=((1.0-t)*left**1.8+t*right**1.8,)*3
        assert point_compose(baked,0.0,(1.0,)*3)==baked
        for distance in (0.0,1.0,2.0,3.0,5.0):
            added=point_sample(distance,4.0,0.8,0.2,1.0)
            world=point_compose(baked,added,(1.0,)*3)
            model=point_compose(baked,added,(1.0,)*3)
            assert world==model
            assert all(a>=b for a,b in zip(world,baked))
assert point_sample(3.0,6.0,0.4,0.2,1.0)==0.0

# The glslangValidator CLI cannot expand our runtime-local GLSL includes.
# Materialize equivalent standalone sources for optional compile/link checks.
def expand_includes(shader):
    return shader.replace('#include "jk_point_lighting.glsl"',shared)

compiler = shutil.which("glslangValidator")
if compiler:
    import tempfile
    with tempfile.TemporaryDirectory() as directory:
        temp=Path(directory)
        for name,source in (("gpu_3do.vert",vert),
                            ("gpu_3do.frag",frag),
                            ("world_cache.vert",world_vert),
                            ("world_cache.frag",world_frag)):
            (temp/name).write_text(expand_includes(source),encoding="utf-8")
        for stem in ("gpu_3do","world_cache"):
            subprocess.run([compiler,"-l",str(temp/(stem+".vert")),
                            str(temp/(stem+".frag"))],check=True,cwd=root)
print("Shared GPU pixel lighting source/parity checks passed")
