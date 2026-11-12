"""Opt-in SSAO at half sample/blur resolution with full-resolution bilateral resolve."""
from pathlib import Path
r=Path(__file__).resolve().parents[1]
read=lambda p:(r/p).read_text(encoding="utf-8")
config=read("common/config.hpp"); parser=read("common/config.cpp")
state=read("renderer/opengl_state.cpp"); ao=read("renderer/renderer_ao_ssao.cpp")
factory=read("renderer/renderer_ao.cpp"); setup=read("renderer/shaders.cpp")
assert "bool experimental_half_res_ssao = false;" in config
assert 'j.contains("experimental_half_res_ssao")' in parser
assert 'the_config->experimental_half_res_ssao' in state
assert 'the_config->experimental_half_res_ssao' in factory
assert "ssao_occlusionbuffer2(screen_res)" in ao
assert "glIsEnabled(GL_SCISSOR_TEST)==GL_FALSE" in ao
assert "gl::set_viewport(ssao_occlusionbuffer.viewport);" in ao
assert 'data_root / (half_res?"shaders/post_ssao_half.frag":' in setup
for path in ("post_ssao_half.frag","post_ssao_half_blur.frag",
             "post_ssao_half_upsample.frag"):
    assert (r/"assets"/"shaders"/path).is_file()
sample=read("assets/shaders/post_ssao_half.frag")
assert "for(int y=0;y<2;++y)for(int x=0;x<2;++x)" in sample
assert "encode_oct(rndnrm)" in sample
assert "vec3 origin = vec3(sample_uv, origin_depth);" in sample
blur=read("assets/shaders/post_ssao_half_blur.frag")
assert "for(int y=-2;y<2;++y)for(int x=-2;x<2;++x)" in blur
assert "out_color=vec4(total/max(weight_sum,1.0),origin.gba);" in blur
up=read("assets/shaders/post_ssao_half_upsample.frag")
assert "vec2 low_position=" in up and "abs(d-s.g)" in up
assert "if(ndot<=0.75)continue;" in up
assert "weights>0.00001?total/weights:0.0" in up
assert 'get_ssao_texture() const' in read("renderer/renderer_ao_ssao.hpp")
assert "return ssao_occlusionbuffer.tex;" in ao
assert "**experimental_half_res_ssao**" in read("doc/config.md")
assert '"experimental_half_res_ssao": false' in read("assets/jkgm.json")
print("Opt-in half-resolution SSAO wiring and reconstruction contracts passed")
