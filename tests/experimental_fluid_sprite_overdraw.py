"""Smoke-only half-resolution source contracts; no OpenGL context required."""
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]

def load(path):
    return (ROOT / path).read_text(encoding="utf-8")

config = load("common/config.hpp")
parser = load("common/config.cpp")
default = load("assets/jkgm.json")
renderer = load("renderer/renderer.cpp")
shader = load("assets/shaders/gpu_3do_preview.frag")
composite = load("assets/shaders/post_smoke_composite.frag")
shaders_h = load("renderer/shaders.hpp")
shaders_cpp = load("renderer/shaders.cpp")
state = load("renderer/opengl_state.cpp")

for name in ("experimental_half_res_fluid_sprites", "fluid_sprite_layer_budget"):
    assert name in config and name in parser and name in default and name in renderer
assert '"experimental_half_res_fluid_sprites": false' in default
assert '"fluid_sprite_layer_budget": 0' in default
assert 'budget<0 || budget>255' in parser
assert 'bool const fluid_sprite=candidate.instance->is_sprite_quad' in renderer
assert 'candidate.texture->blend==material_blend_mode::fluid' in renderer
assert 'draws.resize(write);' in renderer
assert 'draws.swap(smoke_draws);' in renderer
assert 'submit_draws();' in renderer
assert 'glClearBufferiv(GL_STENCIL,0,&stencil_clear)' in renderer
assert 'glStencilFunc(GL_GREATER,static_cast<GLint>(budget),0xff)' in renderer
assert 'glStencilOp(GL_KEEP,GL_KEEP,GL_INCR)' in renderer
assert 'glDisable(GL_DEPTH_TEST);' in renderer
assert 'frame_screen->begin_transparency_pass();' in renderer
assert 'post_smoke_composite->use_program()' in renderer
assert 'glEnable(GL_DEPTH_TEST);' in renderer
assert 'smoke_half_resolution' in shader and 'opaque_scene_pixel()' in shader
assert 'closest=min(closest,texelFetch(scene_depth' in shader
assert 'gl_FragCoord' in shader
assert 'out_color=accumulated/max(weight_sum' in composite
assert 'textureSize(opaque_depth' in composite
assert 'post_smoke_composite_shader' in shaders_h and 'post_smoke_composite_shader' in shaders_cpp
assert 'experimental_half_res_fluid_sprites' in state
# Guard against applying the smoke policy to the world, cutout or opaque passes.
start = renderer.index('bool const split_smoke=transparent && ordinary_pass')
end = renderer.index('if(split_smoke)',start)
assert 'the_config->use_gpu_cache' in renderer[start:end]
assert 'the_config->experimental_half_res_fluid_sprites' in renderer[start:end]
print("half-resolution fluid-SPR routing and stencil budget contracts OK")
