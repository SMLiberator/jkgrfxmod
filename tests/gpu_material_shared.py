"""GPU cached world and 3DO use identical MAT coverage, fetch and emission.
GLSL is expanded at load time; source tests catch accidental divergence.
"""
from pathlib import Path
import re
root=Path(__file__).resolve().parents[1]
read=lambda file:(root/file).read_text(encoding="utf-8")
common=read("assets/shaders/gpu_material_common.glsl")
world=read("assets/shaders/world_cache_texture_preview.frag")
model=read("assets/shaders/gpu_3do_preview.frag")
assert '#include "gpu_material_common.glsl"' in model
assert '#include "gpu_material_common.glsl"' in world
assert "vec4 gpu_material_sample_albedo(" in common
assert "vec3 gpu_material_emission(" in common
assert "bool gpu_material_discard(" in common
for stage in (model,world):
    assert "gpu_material_sample_albedo(" in stage
    assert "gpu_material_emission(" in stage
    assert "gpu_material_discard(" in stage
# The native path never clamps albedo_factor BEFORE multiplying in the
# authored JK vertex lighting. World cache used to clamp factors above one,
# which made textureless color-only replacements visibly darker.
native_opaque=read("assets/shaders/game_opaque_pass.frag")
native_transparent=read("assets/shaders/game_trns_pass.frag")
assert "vec4 albedo = albedo_map_sample * vertex_color * albedo_factor;" in native_opaque
assert "vec4 albedo = albedo_map_sample * vertex_color * albedo_factor;" in native_transparent
assert "vec4 albedo=texel*albedo_factor;" in model
assert ("vec3 albedo=solid_color!=0 ? material_tint.rgb :\n"
        "        texel.rgb*material_tint.rgb;") in world
assert "clamp(texel.rgb*material_tint.rgb,0.0,1.0)" not in world
assert "clamp(material_tint.rgb,0.0,1.0)" not in world
# Numeric example: both no-map paths use a white texel and apply the
# material factor AFTER (or equivalently alongside) baked vertex light.
for factor in (0.25,0.8,1.0,1.5,2.0,4.0):
    for baked in (0.0,0.1,0.3,0.7,1.0):
        native=baked*factor
        world_cache=(1.0*factor)*baked
        model_cache=(1.0*factor)*baked
        assert abs(world_cache-native)<1e-10
        assert abs(model_cache-native)<1e-10
        if factor>1.0 and 0.0<baked<1.0:
            assert min(factor,1.0)*baked < native

# Both world draw passes must bind the identical live MAT factors and maps.
renderer=read("renderer/renderer.cpp")
world_api=read("renderer/shaders.cpp")
policy=read("renderer/world_cache_alpha.hpp")
assert "void bind_world_cache_material(" in renderer
assert "bind_world_cache_material(shader,material," in renderer
assert "bind_world_cache_material(shader,material,face->geometry_mode);" in renderer
assert "shader.set_emissive(material.has_emission," in renderer
assert "shader.set_albedo_transition(material.albedo_source2," in renderer
assert "material.emissive_only);" in renderer
assert "emissive_only=7" not in policy
assert "emissive=7" in policy
assert "policy==world_cache_draw_policy::emissive;" in policy
assert "gl::set_uniform_integer(emissive_only,emission_only?1:0);" in world_api
# Opaque shared material emission also carries environment and direct-light GGX.
# Both belong in the post-SSAO emissive attachment, not the diffuse target.
assert "out_emissive=vec4(emission+env_reflection+direct_specular," in world
assert "out_color=vec4(premultiplied+emission+specular*coverage," in world
# No second G-buffer composition of transparent emission, and the tested
# 90/255 native alpha weighting must remain a world-only operation.
assert "float vertex_rgb_alpha=pow(clamp(face_opacity,0.0,1.0),1.8);" in world
assert "texture_material_alpha*vertex_rgb_alpha" in world
assert "emission*=texture" not in world and "emission*=texture" not in model

# The native path is the visual reference: 32x camera-forward view units
# per JK world unit. All three paths share this EXACT absorption function,
# preserving existing Fluid material values without per-renderer scaling.
native=read("assets/shaders/game_trns_pass.frag")
fluid=read("assets/shaders/fluid_absorption.glsl")
assert "(mat.special && mat.blend_mode!=1 && mat.blend_mode!=2 &&" in policy
assert "mat.blend_mode!=3)" in policy
assert "float world_fluid_absorption(float fragment_depth)" in world
assert "float fluid_absorption(float fragment_depth)" in model
assert "float jk_fluid_absorption(" in fluid
assert "float thickness=max(opaque_depth-fragment_depth,0.0)*ray_ratio;" in fluid
assert "return clamp(1.0-exp(-coefficient*thickness),0.0,1.0);" in fluid
assert "/32.0" not in fluid
for stage in (native,world,model):
    assert stage.count('#include "fluid_absorption.glsl"')==1
    assert "jk_fluid_absorption(" in stage
assert "opaque_depth,vp_z,ray_ratio,fluid_thickness" in native
assert "opaque_depth,fragment_depth,ray_ratio," in world
assert "opaque_depth,fragment_depth,ray_ratio," in model
# Native Fluid uses MATERIAL alpha only in destination blending, not as a
# second multiplication of pigment RGB. JK's vertex alpha, however, was
# premultiplied before gamma conversion and must remain in RGB.
assert "float absorption=world_fluid_absorption(depth);" in world
assert "float opacity=coverage*absorption;" in world
assert "float vertex_rgb_alpha=pow(clamp(face_opacity,0.0,1.0),1.8);" in world
assert "vec3 diffuse=(lit_diffuse*(1.0-fog)*vertex_rgb_alpha+" in world
assert "fog_color*(fog*coverage))*absorption;" in world
assert "out_color=vec4(diffuse+emission+specular,opacity);" in world
assert "out_color=vec4(lit*opacity+emission+specular,opacity);" not in world
assert "float vertex_rgb_alpha=pow(clamp(native_face_opacity,0.0,1.0),1.8);" in model
assert "vec3 diffuse=(lit_rgb*(1.0-fog)*vertex_rgb_alpha+" in model
assert "fog_color*(fog*albedo.a))*absorption;" in model
assert "out_color=vec4(diffuse+emission+specular+projected_light,opacity);" in model
assert "out_color=vec4(rgb*opacity+emission+specular+projected_light,opacity);" not in model
# Normal maps still affect fluid environment/reflection GGX, but they never
# perturb the incoming diffuse normal for world or cached 3DO water.
assert ("bool mapped_diffuse=normal_mapped_diffuse!=0 && solid_color==0 &&\n"
        "        blend_mode!=3 &&") in world
assert "blend_mode!=3 && // Fluid keeps geometry normals for diffuse light" in model
assert "world_mapped_normal(material_texcoord)" in world
assert "mapped_local_normal(uv)" in model
assert "gl::set_active_texture_unit(8);" in renderer
assert "shader.set_scene_depth(8);" in renderer
assert "shader.set_fluid_thickness(material.blend==material_blend_mode::fluid" in renderer
assert "frame_screen->get_resolved_depth_texture()" in renderer
assert "material.fluid_thickness" in renderer
# The opt-in Fluid debug view isolates depth absorption and pigment from
# reflection NaNs. Normal mode must be 0 and must preserve existing blend math.
config=read("common/config.hpp")
parser=read("common/config.cpp")
shader_header=read("renderer/shaders.hpp")
assert "int world_cache_fluid_debug_mode = 0;" in config
assert 'j.contains("world_cache_fluid_debug_mode")' in parser
assert 'world_cache_fluid_debug_mode must be an integer from 0 to 8' in parser
assert "void set_fluid_debug_mode(int mode);" in shader_header
assert "set_fluid_debug_mode(int mode)" in world_api
assert "shader.set_fluid_debug_mode(the_config->world_cache_fluid_debug_mode);" in renderer
assert "uniform int fluid_debug_mode;" in world
assert "if(fluid_debug_mode==1)" in world
assert "out_color=vec4(vec3(absorption),1.0);" in world
assert "if(fluid_debug_mode==2)" in world
assert "out_color=vec4(diffuse,1.0);" in world
assert "if(fluid_debug_mode==3)" in world
assert "any(isnan(env_reflection))" in world
assert "any(isinf(env_reflection))" in world
assert "any(isnan(direct_specular))" in world
assert "any(isinf(direct_specular))" in world
assert "vec4(1.0,0.0,1.0,1.0)" in world  # invalid values are magenta
assert "vec4(0.0,1.0,0.0,1.0)" in world  # finite values are green
# The full diffuse term can only go black after attenuation of albedo,
# original Gouraud/sector lighting, metalness, fog, or face opacity.
# Debug views split those factors; none may replace Fluid rendering by default.
for mode,expr in (
    (4,"vec4(albedo,1.0)"),
    (5,"vec4(lighting,1.0)"),
    (6,"vec4(vec3(env_metalness),1.0)"),
    (7,"vec4(lit_diffuse,1.0)"),
    (8,"vec4(vec3((1.0-fog)*vertex_rgb_alpha*absorption),1.0)"),
):
    assert f"if(fluid_debug_mode=={mode})" in world
    assert "out_color="+expr+";" in world
assert "vec3 lit_diffuse=albedo*lighting*(1.0-env_metalness);" in world
assert "world_cache_fluid_debug_mode must be an integer from 0 to 8" in parser
# A resident MAT may deliberately have NO albedo image (not an unresolved
# native texture). World GeoMode 4 Fluid must use the shared white-source
# shader, remain in the transparent pass, and never request native warmup.
assert "base_source==2 ? vec4(1.0)" in common
assert "world_cache_albedo_ready(mat.albedo_source,mat.has_image)" in policy
assert "world_cache_albedo_ready(material.albedo_source," in renderer
assert "world_cache_albedo_ready(mat.albedo_source," in renderer
assert "albedo_free=" in renderer
assert 'stage="<<(has_albedo?"resident_with_albedo_but_unresolved"' in renderer
# The profiling counter now gathers fluid thickness statistics in the
# same branch; preserve the behavioral assertion without pinning its layout.
assert re.search(r"if\s*\(\s*material\.blend\s*==\s*"
                 r"material_blend_mode::fluid\s*\)\s*\{\s*\+\+fluid\s*;", renderer)

# Reference native vs GPU composition at various authored alpha factors.
# The native D3D vertex is gamma-premultiplied by face opacity, while PNG
# alpha and material albedo_factor.a are NOT applied to pigment RGB again.
# Only absorption and the real coverage multiply the final blend alpha.
def fluid_rgb(alpha,face_alpha,light,pigment,absorption,fog,fog_color):
    coverage=alpha*face_alpha
    vertex_rgb_alpha=face_alpha**1.8
    diffuse=(pigment*light*(1-fog)*vertex_rgb_alpha+
             fog_color*fog*coverage)*absorption
    return diffuse,coverage*absorption

for alpha in (0.15,0.35,0.6,1.0):
    for face_alpha in (90/255,1.0):
        for absorption in (0.05,0.4,0.95,1.0):
            native_rgb,native_a=fluid_rgb(alpha,face_alpha,0.7,0.9,
                                           absorption,0.2,0.5)
            world_rgb,world_a=fluid_rgb(alpha,face_alpha,0.7,0.9,
                                         absorption,0.2,0.5)
            model_rgb,model_a=fluid_rgb(alpha,face_alpha,0.7,0.9,
                                         absorption,0.2,0.5)
            assert native_rgb==world_rgb==model_rgb
            assert native_a==world_a==model_a
            if alpha<1.0 and face_alpha==1.0 and absorption>0.0:
                old_cache_rgb=((0.9*0.7)*(1-0.2)+0.5*0.2)*native_a
                assert old_cache_rgb<native_rgb

# Numeric parity and stability: no receiver is infinitely deep; zero
# thickness is optically clear; greater ray length increases absorption.
from math import exp
def fluid_absorption(coefficient, fragment_depth, opaque_depth, ray_ratio):
    value=1.0 if coefficient>0 else 0.0
    if opaque_depth<65500:
        thickness=max(opaque_depth-fragment_depth,0.0)*ray_ratio
        value=1.0-exp(-coefficient*thickness)
    return min(max(value,0.0),1.0)

assert fluid_absorption(.25,32.0,65504.0,1.0)==1.0
assert fluid_absorption(0.0,32.0,65504.0,1.0)==0.0
assert fluid_absorption(.25,64.0,64.0,1.0)==0.0
assert fluid_absorption(.025,64.0,66.0,1.0)<fluid_absorption(.025,64.0,68.0,1.0)
assert fluid_absorption(.025,64.0,66.0,2.0)>fluid_absorption(.025,64.0,66.0,1.0)
# Regression: the previous GPU /32 normalization made a material with
# coefficient .25 and native depth difference 32 almost transparent.
native_opaque=fluid_absorption(.25,64.0,96.0,1.0)
old_gpu_opaque=1.0-exp(-.25*((96.0-64.0)/32.0))
assert native_opaque>.999 and .20<old_gpu_opaque<.24

# Shader should never evaluate independent copies of these functions.
assert "vec4 gpu_material_sample_albedo(" not in model
assert "vec3 gpu_material_emission(" not in model
assert "bool gpu_material_discard(" not in model

def discard(alpha,mode,emission_only=False,transparent=False,post=False):
    if post:
        if mode==2 and alpha<0.5:return True
        if mode in (0,2,4):alpha=1.0
        return alpha<=.001 and not emission_only
    if not transparent:
        return mode==3 or mode==1 and alpha<.999 or mode==2 and alpha<.5
    return mode in (0,2,4) or alpha<=.001 and not emission_only or mode==1 and alpha>=.999

assert not discard(0.0,0,False,False,True) # opaque HUD keeps solid coverage
assert discard(0.0,1,False,False,True) # mixed HUD honors actual alpha
assert discard(0.0,3,True,False)  # emissive-only never writes opaque depth
assert not discard(0.0,3,True,True)  # but emits in the forward pass
assert discard(0.0,3,False,True)
assert not discard(1.0,1,False,False)
assert discard(1.0,1,False,True)
assert discard(0.5,1,False,False)
assert not discard(0.5,1,False,True)
assert discard(1.0,3,False,False)
assert not discard(1.0,3,False,True)
print("GPU common material source checks passed")
