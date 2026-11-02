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
assert "out_emissive=vec4(emission+env_reflection," in world
assert "out_color=vec4(premultiplied+emission+specular*coverage," in world
# No second G-buffer composition of transparent emission, and the tested
# 90/255 native alpha weighting must remain a world-only operation.
assert "float vertex_rgb_alpha=pow(clamp(face_opacity,0.0,1.0),1.8);" in world
assert "texture_material_alpha*vertex_rgb_alpha" in world
assert "emission*=texture" not in world and "emission*=texture" not in model

# World Fluid shares the 3DO depth-absorption law, using camera/world-space
# position for the same ray-length correction and opaque R16F depth sampler.
assert "(mat.special && mat.blend_mode!=1 && mat.blend_mode!=2 &&" in policy
assert "mat.blend_mode!=3)" in policy
assert "float world_fluid_absorption(float fragment_depth)" in world
assert "float fluid_absorption(float fragment_depth)" in model
assert "float thickness=max((opaque_depth-fragment_depth)/32.0,0.0)*ray_ratio;" in world
assert "float thickness=max((opaque_depth-fragment_depth)/32.0,0.0)*ray_ratio;" in model
assert "opacity=1.0-exp(-fluid_thickness*thickness);" in world
assert "opacity=1.0-exp(-advanced_material_params.y*thickness);" in model
assert "float opacity=coverage*world_fluid_absorption(depth);" in world
assert "out_color=vec4(lit*opacity+emission+specular,opacity);" in world
assert "gl::set_active_texture_unit(8);" in renderer
assert "shader.set_scene_depth(8);" in renderer
assert "shader.set_fluid_thickness(material.blend==material_blend_mode::fluid" in renderer
assert "frame_screen->get_resolved_depth_texture()" in renderer
assert "material.fluid_thickness" in renderer
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

# Fluid opacity remains clear at zero thickness, more opaque with depth,
# and fully opaque when no solid receiver exists behind positive absorption.
from math import exp
def fluid_absorption(coefficient, fragment_depth, opaque_depth, ray_ratio):
    value=1.0 if coefficient>0 else 0.0
    if opaque_depth<65500:
        thickness=max((opaque_depth-fragment_depth)/32.0,0.0)*ray_ratio
        value=1.0-exp(-coefficient*thickness)
    return min(max(value,0.0),1.0)

assert fluid_absorption(.25,32.0,65504.0,1.0)==1.0
assert fluid_absorption(0.0,32.0,65504.0,1.0)==0.0
assert fluid_absorption(.25,64.0,64.0,1.0)==0.0
assert fluid_absorption(.25,64.0,96.0,1.0)<fluid_absorption(.25,64.0,128.0,1.0)
assert fluid_absorption(.25,64.0,96.0,2.0)>fluid_absorption(.25,64.0,96.0,1.0)

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
