"""Half-resolution opt-in spatial blur must preserve the default render path.

The new shader runs only when enabled and must still output a full-res
scene for unchanged bloom, HDR/color-grading and HUD composition.
"""
from pathlib import Path
root=Path(__file__).resolve().parents[1]
read=lambda path:(root/path).read_text(encoding="utf-8")
config=read("common/config.hpp")
parser=read("common/config.cpp")
docs=read("doc/config.md")
state_h=read("renderer/opengl_state.hpp")
state=read("renderer/opengl_state.cpp")
shader_h=read("renderer/shaders.hpp")
shader_cpp=read("renderer/shaders.cpp")
blur=read("renderer/spatialblur.hpp")
original=read("assets/shaders/post_spatial_blur.frag")
fast=read("assets/shaders/post_spatial_blur_half_res.frag")

key="experimental_spatial_blur_half_res"
assert f"bool {key} = false;" in config
assert f'j.contains("{key}")' in parser
assert f"rv->{key}" in parser
assert f"`{key}`" in docs
assert f"bool {key} = false;" in state_h
assert "the_config->experimental_spatial_blur_half_res" in state
assert "std::unique_ptr<shaders::post_spatial_blur_shader> post_spatial_blur_fast;" in state_h
assert "std::unique_ptr<post_buffer> spatial_blur_half_work;" in state_h
assert "if(experimental_spatial_blur_half_res) {" in state
assert "std::make_unique<post_buffer>(" in state
assert "std::max(1,get<x>(screen_res)/2)" in state
assert "std::max(1,get<y>(screen_res)/2)" in state

# Legacy shader remains a separate file and the original path can be chosen.
assert 'shaders/post_spatial_blur.frag' in shader_cpp
assert 'shaders/post_spatial_blur_half_res.frag' in shader_cpp
assert "bool half_resolution=false" in shader_h
assert "void set_fast_inputs(" in shader_h
assert "post_spatial_blur_shader::set_fast_inputs(" in shader_cpp
assert "glIsEnabled(GL_SCISSOR_TEST)==GL_FALSE" in blur
assert "auto &shader=halfres" in blur
assert "?*ogs->post_spatial_blur_fast:ogs->post_spatial_blur;" in blur
assert "?*ogs->spatial_blur_half_work:ogs->spatial_blur_work;" in blur
assert "shader.set_fast_inputs(2," in blur
assert "gl::set_viewport(work.viewport);" in blur
assert "gl::set_viewport(ogs->spatial_blur_result.viewport);" in blur
assert "return ogs->spatial_blur_result.tex;" in blur

# Full-res scene is sampled for inactive blur pixels and not modified.
assert "uniform sampler2D original_image;" in fast
assert "uniform vec2 output_size;" in fast
assert "vec2 uv = gl_FragCoord.xy / output_size;" in fast
assert "texelFetch(original_image,pixel,0)" in fast
assert "if(amount < 0.001)" in fast
assert "out_color = source;" in fast
assert "texture(depth_image,uv)" in fast
assert "vec2 step_uv = direction * (3.0 * amount) / screen_size;" in fast
assert "const float weights[5]" in original and "const float weights[5]" in fast
assert "for(int i = 1; i <= 4; ++i)" in original and "for(int i = 1; i <= 4; ++i)" in fast

# Visual-sensitive postprocessing sources aren't part of the experiment.
tone=read("assets/shaders/post_to_srgb.frag")
assert "float filmic_luminance(float x)" in tone
assert "hdr_color_crispness>0.0" in tone
assert "color += brightness;" in tone
assert "color = (color - 0.5) * contrast + 0.5;" in tone
print("Experimental half-res spatial blur source contracts passed")
