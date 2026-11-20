"""Dual half-resolution spatial blur is permanent; full-res scissor fallback remains."""
from pathlib import Path

root=Path(__file__).resolve().parents[1]
read=lambda p:(root/p).read_text(encoding="utf-8")
cfg=read("common/config.hpp")
parser=read("common/config.cpp")
doc=read("doc/config.md")
state_h=read("renderer/opengl_state.hpp")
state=read("renderer/opengl_state.cpp")
blur=read("renderer/spatialblur.hpp")
sh=read("renderer/shaders.hpp")
sc=read("renderer/shaders.cpp")
half=read("assets/shaders/post_spatial_blur_half_res.frag")
for key in ("spatial_blur_mode", "experimental_spatial_blur_mode",
            "experimental_spatial_blur_half_res"):
    assert key not in cfg and key not in parser and key not in state_h
    assert key not in state and key not in blur and key not in doc
assert "spatial_blur_half_work=std::make_unique<post_buffer>(half_size);" in state
assert "spatial_blur_half_result=std::make_unique<post_buffer>(half_size);" in state
assert "post_spatial_blur_reconstruct=std::make_unique<" in state
assert "bool const halfres=ogs->post_spatial_blur_fast" in blur
assert "glIsEnabled(GL_SCISSOR_TEST)==GL_FALSE" in blur
assert "auto &shader=halfres" in blur
assert "?*ogs->post_spatial_blur_fast:ogs->post_spatial_blur;" in blur
assert "auto &vertical_target=halfres" in blur
assert blur.count("shader.set_output_size(")==2
assert "void set_output_size(size<2,float> output_dims);" in sh
assert "post_spatial_blur_shader::set_output_size(" in sc
for source in (blur,sh,sc,half):
    assert "set_fast_inputs" not in source
    assert "restore_original" not in source
assert "uniform sampler2D original_image;" not in half
assert "uniform vec2 output_size;" in half
assert "vec4 source = texture(source_image,uv);" in half
assert "texelFetch(original_image,pixel,0)" not in half
# Only the full-res reconstruction pass needs the original image.
reconstruct=blur.split("auto &rebuild=*ogs->post_spatial_blur_reconstruct;",1)[1]
assert "gl::set_active_texture_unit(2);" in reconstruct
assert "gl::bind_texture(gl::texture_bind_target::texture_2d,source);" in reconstruct
assert "if(halfres) {" in blur
assert "gl::set_viewport(ogs->spatial_blur_result.viewport);" in blur
assert "return ogs->spatial_blur_result.tex;" in blur
assert "class post_spatial_blur_reconstruct_shader : public opengl_shader" in sh
assert "post_spatial_blur_reconstruct_shader::set_inputs(" in sc
for p in ("post_spatial_blur.frag","post_spatial_blur_half_res.frag",
          "post_spatial_blur_reconstruct.frag"):
    assert (root/"assets"/"shaders"/p).is_file()
recon=read("assets/shaders/post_spatial_blur_reconstruct.frag")
assert "texture(depth_image,tap_uv)" in recon
assert "smoothstep(0.0,0.25,amount)" in recon
print("Standard dual half-resolution spatial blur contracts passed")
