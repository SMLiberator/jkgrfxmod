"""Cached-emission alpha routing matches native normal/Fluid behavior.

Run in CI alongside C++ alpha partition regressions. Explicit mask/dither
still discard emission as the native GLSL shaders do.
"""
from pathlib import Path

root = Path(__file__).resolve().parents[1]
read = lambda p: (root / p).read_text(encoding="utf-8")
shared = read("assets/shaders/gpu_material_common.glsl")
model = read("assets/shaders/gpu_3do_preview.frag")
world = read("assets/shaders/world_cache.frag")
native = read("assets/shaders/game_trns_pass.frag")

for shader in (model, world):
    assert shader.index("vec3 emission=") < shader.index("if(gpu_material_discard(")
    assert "gpu_material_emission_coverage(" in shader
    assert "int const preserve_emission" not in shader  # GLSL qualifiers precede type
    assert "preserve_emission,transparent_pass!=0" in shader
    assert shader.count("vec3 emission=") == 1
assert "return any(notEqual(emission,vec3(0.0))) ? 1 : 0;" in shared
assert "if(emissive_only!=0)return 1;" in shared
assert "if(blend_mode!=0 && blend_mode!=3)return 0;" in shared
assert "if(policy==1 && alpha<0.999)return true;" in shared
assert "if(policy==2 && alpha<cutoff)return true;" in shared
assert "if(policy==4 && alpha<=gpu_material_dither_threshold())return true;" in shared
assert "if(policy==0 || policy==2 || policy==4)return true;" in shared
assert "if(alpha<=0.001 && emission_only==0)return true;" in shared
assert "if(alpha_cutout!=0 && coverage<alpha_cutoff)discard;" in world
assert "if(decal_receiver_lighting==0 && native_face_opacity>0.001)" in model
assert "if(albedo_map_sample.a < alpha_cutoff) discard;" in native
assert "if(albedo.a <= dither_threshold()) discard;" in native
assert "out_color = vec4(color, albedo.a);" in native
assert "out_color=vec4(premultiplied+emission+specular*coverage," in world
assert "rgb*a+emission+specular*a+projected_light" in model

def preserves_emission(material_emission_only, blend, rgb):
    return material_emission_only or (blend in (0, 3) and any(c != 0 for c in rgb))

def visible(alpha, policy, preserve, transparent, post=False, cutoff=0.5):
    if post:
        if policy == 2 and alpha < cutoff: return False
        if policy == 4 and alpha <= .5: return False
        if policy in (0, 2, 4): alpha=1.
        return alpha > .001 or preserve
    if not transparent:
        if policy == 3 or (policy == 1 and alpha < .999): return False
        if policy == 2 and alpha < cutoff: return False
        if policy == 4 and alpha <= .5: return False
        return True
    if policy in (0, 2, 4): return False
    if alpha <= .001 and not preserve: return False
    if policy == 1 and alpha >= .999: return False
    return True

for policy in (1, 3):  # mixed alpha and forced blended
    for blend in (0, 3):  # normal, Fluid
        lit=preserves_emission(False,blend,(1.,0.,0.))
        black=preserves_emission(False,blend,(0.,0.,0.))
        assert lit and not black
        assert not visible(0.,policy,lit,False)  # never write opaque depth
        assert visible(0.,policy,lit,True)       # keep independent RGB
        assert not visible(0.,policy,black,True) # truly empty pixels discarded
        assert visible(.5,policy,black,True)     # ordinary fractional base
        assert visible(0.,policy,lit,True,post=True) # post-exempt HUD
    for blend in (1,2):
        assert not preserves_emission(False,blend,(1.,0.,0.))
assert visible(1.,1,True,False) and not visible(1.,1,True,True)
for mode in (2,4):  # explicit silhouette masking
    assert not visible(0.,mode,True,False)
    assert not visible(0.,mode,True,True)
    assert not visible(0.,mode,True,True,post=True)
assert preserves_emission(True,1,(0.,0.,0.)) # existing emissive-only override
assert visible(0.,3,True,True)
print("Cached emissive alpha parity regression passed")
