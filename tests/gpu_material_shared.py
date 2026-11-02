"""GPU cached world and 3DO use identical MAT coverage, fetch and emission.
GLSL is expanded at load time; source tests catch accidental divergence.
"""
from pathlib import Path
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
# Shader should never evaluate independent copies of these functions.
assert "vec4 gpu_material_sample_albedo(" not in model
assert "vec3 gpu_material_emission(" not in model
assert "bool gpu_material_discard(" not in model

def discard(alpha,mode,emission_only=False,transparent=False,post=False):
    if post:
        if mode==2 and alpha<0.5:return True
        return alpha<=.001 and not emission_only
    if not transparent:
        return mode==3 or mode==1 and alpha<.999 or mode==2 and alpha<.5
    return mode in (0,2,4) or alpha<=.001 and not emission_only or mode==1 and alpha>=.999

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
