"""Regression contracts for all three experimental spatial-blur modes."""
from pathlib import Path
root=Path(__file__).resolve().parents[1]
read=lambda path:(root/path).read_text(encoding="utf-8")
cfg=read("common/config.hpp"); parser=read("common/config.cpp")
doc=read("doc/config.md"); state_h=read("renderer/opengl_state.hpp")
state=read("renderer/opengl_state.cpp")
sh=read("renderer/shaders.hpp"); sc=read("renderer/shaders.cpp")
blur=read("renderer/spatialblur.hpp")
full=read("assets/shaders/post_spatial_blur.frag")
fast=read("assets/shaders/post_spatial_blur_half_res.frag")
recon=read("assets/shaders/post_spatial_blur_reconstruct.frag")
assert "int experimental_spatial_blur_mode = 0;" in cfg
assert "bool experimental_spatial_blur_half_res = false;" in cfg
assert 'j.contains("experimental_spatial_blur_mode")' in parser
assert "rv->experimental_spatial_blur_mode=mode.get<int>();" in parser
assert "rv->experimental_spatial_blur_mode=1;" in parser
assert "experimental_spatial_blur_mode must be 0, 1 or 2" in parser
for k in ("experimental_spatial_blur_mode","experimental_spatial_blur_half_res"):
    assert f"`{k}`" in doc
assert "int experimental_spatial_blur_mode = 0;" in state_h
assert "the_config->experimental_spatial_blur_mode" in state
assert "if(experimental_spatial_blur_mode>=1) {" in state
assert "if(experimental_spatial_blur_mode==2) {" in state
assert "std::unique_ptr<post_buffer> spatial_blur_half_result;" in state_h
assert "spatial_blur_half_result=std::make_unique<post_buffer>(half_size);" in state
assert "std::unique_ptr<shaders::post_spatial_blur_reconstruct_shader>" in state_h
assert "shaders/post_spatial_blur.frag" in sc
assert "shaders/post_spatial_blur_half_res.frag" in sc
assert "shaders/post_spatial_blur_reconstruct.frag" in sc
assert "class post_spatial_blur_reconstruct_shader : public opengl_shader" in sh
assert "post_spatial_blur_reconstruct_shader::set_inputs(" in sc
assert "bool const dual_halfres=halfres &&" in blur
assert "ogs->experimental_spatial_blur_mode==2" in blur
assert "glIsEnabled(GL_SCISSOR_TEST)==GL_FALSE" in blur
assert "auto &shader=halfres" in blur
assert "?*ogs->post_spatial_blur_fast:ogs->post_spatial_blur;" in blur
assert "auto &work=halfres" in blur
assert "gl::set_viewport(vertical_target.viewport);" in blur
assert "gl::set_viewport(ogs->spatial_blur_result.viewport);" in blur
assert "gl::texture_view apply_spatial_blur(" in blur
assert "return ogs->spatial_blur_result.tex;" in blur
assert "uniform int restore_original;" in fast
assert "restore_original!=0" in fast
assert "texelFetch(original_image,pixel,0)" in fast
assert "vec2 step_uv = direction * (3.0 * amount) / screen_size;" in fast
assert "const float weights[5]" in full and "const float weights[5]" in fast
assert "uniform sampler2D original_image;" in recon
assert "texelFetch(original_image,pixel,0)" in recon
assert "if(amount<0.001)" in recon
assert "ivec2 dims=textureSize(blur_image,0);" in recon
assert "for(int y=0;y<2;++y)" in recon
assert "for(int x=0;x<2;++x)" in recon
assert "texture(depth_image,tap_uv)" in recon
assert "smoothstep(0.0,0.25,amount)" in recon
# Tone mapping and grading are never altered by the reconstruction.
tone=read("assets/shaders/post_to_srgb.frag")
assert "float filmic_luminance(float x)" in tone
assert "color += brightness;" in tone
assert "color = (color - 0.5) * contrast + 0.5;" in tone
print("All experimental spatial blur mode source contracts passed")
