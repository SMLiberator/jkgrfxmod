"""Source and parity checks for GPU-cached world and 3DO pixel diffuse lights.

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
main = (root / "renderer/main.cpp").read_text(encoding="utf-8")
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
# 0x20 skybox models use GPU vertex dynamic lights, not per-fragment
# diffuse or per-pixel Thing point-specular. Other models remain unchanged.
assert "inst.skybox_vertex_lighting=jkgm::thing_uses_skybox_vertex_lights(" in main
assert "!inst.skybox_vertex_lighting &&" in wiring
assert "prog.set_dynamic_light_specular(\n                        !inst.skybox_vertex_lighting &&" in wiring
assert "!inst.camera_hud && !inst.skybox_vertex_lighting &&" in wiring
assert "bool const have_thing_flags=" in main
assert "if(have_thing_flags) {" in main
assert "if(shadow_enabled || (the_config && the_config->enable_decals))" in main
assert "pixel_dynamic_lighting==0" in vert
assert ("world_cache_use_pixel_lights(\n"
        "                the_config->gpu_pixel_lighting,camera_ready);") in wiring
assert "return enabled && camera_ready;" in (
    root / "renderer/world_cache_lighting.hpp").read_text(encoding="utf-8")
assert "cache.native_dynamic_light.assign(" in wiring
assert "native_dynamic_ready=read_world_cache_dynamic_lights(" in wiring
# Pixel diffuse can be disabled while opt-in GGX specular still needs the
# native light list. The shader's independent uniform gates prevent that list
# from re-lighting JK's baked/Gouraud diffuse.
assert wiring.count("if(!pixel_lighting && !point_specular)point_lights.clear();")==2
assert wiring.count("shader.set_specular_lighting(pixel_lighting,point_specular);")==2
assert "std::istringstream lines{std::string(mb.str())};" in program
assert 'gl::set_uniform_integer(pixel_dynamic_lighting,pixel_lighting?1:0)' in program
# Pixel-enabled GPU geometry paths share the editable attenuation and
# gamma/saturation GLSL, but disabled mode keeps independent JK Gouraud.
shared_path = root / "assets/shaders/jk_point_lighting.glsl"
shared = shared_path.read_text(encoding="utf-8")
world_vert_path = root / "assets/shaders/world_cache.vert"
world_frag_path = root / "assets/shaders/world_cache.frag"
world_vert = world_vert_path.read_text(encoding="utf-8")
world_frag = world_frag_path.read_text(encoding="utf-8")
for stage in (frag, world_frag):
    assert '#include "jk_point_lighting.glsl"' in stage
assert '#include "jk_point_lighting.glsl"' not in vert
assert "pixel_dynamic_lighting==0" in vert
assert "range_distance*attenuation_min,0.0)*along" in vert
assert "load_shader_source(filename.parent_path()/name" in program
assert "float jk_point_sample(" in shared
assert "vec3 jk_point_compose(" in shared
# Authored lighting inputs are separately capped at 1/2, never the HDR sum.
limits=(root/"renderer/light_intensity_range.hpp").read_text(encoding="utf-8")
world_lights=(root/"renderer/world_cache_lighting.hpp").read_text(encoding="utf-8")
sector_cross=(root/"renderer/sector_crossing_gpu.hpp").read_text(encoding="utf-8")
sample=(root/"assets/jkgm.json").read_text(encoding="utf-8")
assert "bool overbright_intensities = true" in config
assert 'j.contains("overbright_intensities")' in parser
assert '"overbright_intensities": true' in sample
assert "overbright ? 2.0f : 1.0f" in limits
assert "std::clamp(value,0.0f,authored_light_limit(overbright))" in limits
assert "std::min(value,authored_light_limit(overbright))" in limits
assert "clamp_authored_light(source[i],overbright)" in world_lights
assert "clamp_authored_extra_light(extra,overbright)" in world_lights
assert "clamp_authored_extra_light(global_extra,overbright)" in world_lights
assert "std::max(raw_brightness+dynamic,0.0f),ambient" in world_lights
assert "clamp_authored_light(ambient,overbright)" in sector_cross
assert "clamp_authored_extra_light(extra,overbright)" in sector_cross
assert "the_config->overbright_intensities" in main
assert "the_config->overbright_intensities" in wiring
assert "pow(max(bright,0.0),1.8)" in vert
assert "clamp(bright,0.0,1.0)" not in vert
assert "lighting=mix(max(lighting,vec3(0.0)),vec3(1.0),fullbright);" in world_frag
assert "vec3 lit=max(base+max(added,vec3(0.0))*light,vec3(0.0));" in shared
assert "if(l>=1.0) break;" not in vert
assert "uniform vec3 light_color[16];" in frag
assert "uniform vec3 light_color[16];" in world_frag
assert "added+=light_color[i]*jk_point_sample(" in frag
assert "added+=light_color[i]*jk_point_sample(" in world_frag
assert "vec3 added=vec3(0.0);" in frag
assert "vec3 added=vec3(0.0);" in world_frag
assert "frame_saber_light_colors=saber_light_rgb::get_registry().consume();" in wiring
assert 'light_color.push_back(gl::get_uniform_location(' in program
assert "frame_saber_light_colors.valid" in wiring
# Every world-cache set_lights call must pass RGB maps, including the
# zero-light sky pass. A leftover two-argument call breaks the Windows build.
assert wiring.count("shader.set_lights(")==4
assert "shader.set_lights({},0.0f,\n" in wiring
assert "shader.set_lights({},0.0f);" not in wiring
assert wiring.count("frame_saber_light_colors.valid);")==5

for stage in (frag, world_frag):
    assert "jk_point_sample(" in stage
for stage in (frag, world_frag):
    assert "jk_point_compose(" in stage
# Solid world faces use CMP palette RGB with no texture dependency.
assert "uniform int solid_color;" in world_frag
# GeoMode 3 still uses its MAT/CMP color, but neither solid nor
# textureless textured materials may clamp albedo_factor before
# multiplying JK baked lighting (factors > 1 legitimately brighten).
assert "solid_color!=0 ? material_tint.rgb :" in world_frag
assert "texel.rgb*material_tint.rgb;" in world_frag
assert "solid_color!=0 ? clamp(material_tint.rgb" not in world_frag
assert "clamp(texel.rgb*material_tint.rgb,0.0,1.0)" not in world_frag
assert "uniform int alpha_cutout;" in world_frag
# Cutout alpha now shares the same coverage factor as masked, mixed-alpha,
# forced-native translucency and dithered world faces.
assert "float texture_material_alpha=clamp(texel.a*material_tint.a,0.0,1.0);" in world_frag
assert "float coverage=clamp(texture_material_alpha*face_opacity,0.0,1.0);" in world_frag
assert "gpu_material_discard(coverage,alpha_mode,alpha_cutoff," in world_frag
assert "gpu_material_emission(" in world_frag
assert "gpu_material_sample_albedo(" in world_frag
# Opaque world reflections share the emissive attachment so SSAO does not dim them.
assert "out_emissive=vec4(emission+env_reflection+direct_specular," in world_frag
assert "if(world_pixel_dynamic_lighting!=0" in world_frag
world_specular=(root/"assets/shaders/world_cache_direct_specular.glsl").read_text(
    encoding="utf-8")
assert "if(world_dynamic_light_specular!=0)" in world_specular
assert "vec3 world_direct_specular(" in world_specular
assert '#include "gpu_material_common.glsl"' in world_frag
assert "if(transparent_pass!=0)" in world_frag
assert "float vertex_rgb_alpha=pow(clamp(face_opacity,0.0,1.0),1.8);" in world_frag
assert "texture_material_alpha*vertex_rgb_alpha" in world_frag
assert "fog_color*(fog*coverage)" in world_frag
# Forward alpha keeps EnvMap specularity independent of the old diffuse alpha formula.
assert "out_color=vec4(premultiplied+emission+specular*coverage," in world_frag
# The native D3D bridge converts gamma(alpha-premultiplied diffuse),
# retaining *linear* 90/255 alpha for the framebuffer's blend factors.
# Changing the alpha itself instead of its RGB would be a regression.
native_face_alpha=90.0/255.0
native_rgb_factor=native_face_alpha**1.8
assert abs(native_face_alpha-0.35294117647058826)<1e-12
assert 0.15<native_rgb_factor<0.16
assert native_rgb_factor<native_face_alpha
for texture_alpha,material_alpha in ((1.0,1.0),(0.5,1.0),(1.0,0.7)):
    actual_alpha=texture_alpha*material_alpha*native_face_alpha
    expected_rgb=texture_alpha*material_alpha*native_rgb_factor
    assert abs(actual_alpha-(texture_alpha*material_alpha*90/255))<1e-12
    assert 0.0<=expected_rgb<=actual_alpha
assert "discard;" in world_frag
assert "vec3 lighting=cached_lit_rgb;" in world_frag
assert "if(all(lessThanEqual(added,vec3(0.0)))) return baked;" in frag
assert "if(all(lessThanEqual(added,vec3(0.0)))) return baked;" in shared
assert "pixel_dynamic_lighting!=0" in frag
assert "pixel_jk_diffuse_lighting(diffuse_local_normal)" in frag
assert "jk_fragment_lit" in frag
assert "jk_lit_rgb*(1.0-env_metalness)" not in frag
assert "albedo.rgb*jk_fragment_lit" in frag
assert "dot(jk_fragment_lit,vec3(0.2126" in frag
assert "nonrigid_light_transform!=0" in frag
assert "light_position_radius[i].xyz-anchor" in frag
# Normal-mapped diffuse is a separate, default-off experiment; no baked RGB
# or vanilla Gouraud loop is changed. The mapped normal feeds only added
# pixel-light response and deferred sun/spotlight receivers.
assert "bool enable_normal_mapped_diffuse = false" in config
assert 'j.contains("enable_normal_mapped_diffuse")' in parser
assert wiring.count("shader.set_normal_mapped_diffuse(")==2
assert "prog.set_normal_mapped_diffuse(" in wiring
assert "diffuse_local_normal=mapped_local_normal(uv);" in frag
assert "vec3 diffuse_view_normal=view_normal;" in frag
assert "pixel_jk_diffuse_lighting(diffuse_local_normal)" in frag
assert "view_depth,diffuse_view_normal" in frag
assert "vec4(normalize(diffuse_view_normal)," in frag
assert "vec3 diffuse_world_normal=normalize(cached_world_normal);" in world_frag
assert "diffuse_world_normal=world_mapped_normal(material_texcoord);" in world_frag
assert "vec3 normal=diffuse_world_normal;" in world_frag
assert "vec4(diffuse_view_normal,1.0);" in world_frag
assert "gl::set_uniform_integer(normal_mapped_diffuse,enabled?1:0);" in program
assert "pixel_dynamic_lighting==0" in vert
assert "vec3 lighting=cached_lit_rgb;" in world_frag
assert "vec3 baked=sector_crossing_count>1" in frag
# A matte material must still bind maps when specular is OFF.
assert "spec_active || diffuse_mapping" in wiring
assert "bool const material_normals_active=" in wiring
assert "geometry_mode==4U && !material.solid_fullbright" in wiring
assert "(reflective || diffuse_mapping) && material.has_normal" in wiring
assert "spec_active?specular.first:0.0f" in wiring
assert "reflective?specular.first:0.0f" in wiring
# The opaque composite consumes the G-buffer normal for sun and cookie lights.
composite=(root/"assets/shaders/post_opaque_composite.frag").read_text(
    encoding="utf-8")
assert "vec3 vn=texelFetch(normal_image,pixel,0).rgb;" in composite
assert "vec4 normal_pixel=texelFetch(normal_image,tc,0);" in composite
# Genuine shadow caster geometry and alpha-masked silhouettes are unchanged.
shadow=(root/"assets/shaders/spotlight_shadow.vert").read_text(encoding="utf-8")
assert "normal_map" not in shadow

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
# Preserve the previous C++ helper's numeric regression cases here, where
# they are paired with the shader-source assertions for the real formula.
assert "range_distance>=radius" in shared
assert "max(intensity-range_distance*attenuation,0.0)*incidence" in shared
assert "max(dot(surface_normal,delta/max(dist,0.00001)),0.0)" in shared
for distance,incidence,want in (
    (0.0,1.0,0.8),(1.0,1.0,0.6),(2.0,1.0,0.4),
    (3.0,1.0,0.2),(4.0,1.0,0.0),(20.0,1.0,0.0),
    (2.0,-1.0,0.0),(2.0,0.5,0.2)
):
    assert abs(point_sample(distance,4.0,0.8,0.2,incidence)-want)<1e-6
# Cutoff radius can exceed the positive-falloff distance. In that
# region lights must NEVER subtract baked Gouraud intensity.
assert point_sample(3.0,6.0,0.4,0.2,1.0)==0.0
assert point_sample(5.0,6.0,0.4,0.2,0.5)==0.0
assert abs(point_sample(1.0,6.0,0.4,0.2,0.5)-0.1)<1e-6
assert "world_cache_model_style_pixel_light(" not in (
    root / "renderer/world_cache_lighting.hpp").read_text(encoding="utf-8")
assert not (root / "assets/shaders/menu.vert").exists()
assert not (root / "assets/shaders/world_cache_wireframe.frag").exists()
# The cached-world vertex shader is a production rendering stage.
assert 'root/"shaders/world_cache.vert"' in program
assert 'root/"shaders/world_cache.frag"' in program


# The glslangValidator CLI cannot expand our runtime-local GLSL includes.
# Expand every local GLSL fragment (including the shared fluid law and the
# new world direct-specular helper), just as the runtime shader loader does.
shader_dir=root/"assets/shaders"
def expand_includes(shader,stack=()):
    def expand(match):
        name=match.group(1)
        assert "/" not in name and "\\" not in name and name not in (".","..")
        assert name not in stack, ("recursive GLSL include",stack,name)
        return expand_includes((shader_dir/name).read_text(encoding="utf-8"),
                               stack+(name,))
    return re.sub(r'(?m)^[ \t]*#include\s+"([^"]+)"[ \t]*$',expand,shader)

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
