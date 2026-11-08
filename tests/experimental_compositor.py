"""Default-off GPU compositor and native triangle upload experiments."""
from pathlib import Path
root=Path(__file__).resolve().parents[1]
read=lambda p:(root/p).read_text(encoding="utf-8")
c=read("common/config.hpp"); parser=read("common/config.cpp")
doc=read("doc/config.md"); st=read("renderer/opengl_state.hpp")
ctor=read("renderer/opengl_state.cpp")
bm=read("renderer/renderer_compositor_bloom.cpp")
s=read("assets/shaders/post_scale.frag")
base=read("assets/shaders/post_low_pass.frag")
sc=read("renderer/shaders.cpp")
r=read("renderer/renderer.cpp")
keys=["experimental_fused_bloom_downsample","experimental_skip_bloom_clears",
      "experimental_native_triangle_uploads"]
for k in keys:
    assert f"bool {k} = false;" in c
    assert f'j.contains("{k}")' in parser
    assert f'get_to(\n                rv->{k})' in parser
    assert f"**{k}**" in doc
for k in keys[:2]:
    assert f"bool {k} = false;" in st
    assert f"the_config->{k}" in ctor
assert "if(!fused_bloom) {" in bm
assert "ogs->post_low_pass.use_program();" in bm
assert "bind_bloom_exposure(ogs);" in bm
assert "src_tx=fused_bloom" in bm
assert "fused_bloom && level==0U" in bm
assert "set_fused_bloom(" in sc
assert "if(fused_bloom_first_pass!=0)" in s
assert "exp2(clamp(ev,-5.0,5.0))" in s
assert "0.2125,0.7154,0.0721" in s
assert "0.2125, 0.7154, 0.0721" in base
assert "pow(lum,3.0)" in s and "pow(lum, bloom_curve_power)" in base
assert "smoothstep(0.0,1.0,x)" in s
assert "a=bloom_threshold(a,exposure_factor);" in s
assert "b=bloom_threshold(b,exposure_factor);" in s
assert "c=bloom_threshold(c,exposure_factor);" in s
assert "d=bloom_threshold(d,exposure_factor);" in s
assert bm.count("if(!skip_bloom_clears)") == 4
assert "glIsEnabled(GL_SCISSOR_TEST)==GL_FALSE" in bm
assert "gl::clear({gl::clear_flag::color, gl::clear_flag::depth});" in bm
assert "if(the_config->experimental_native_triangle_uploads &&" in r
assert "tb.size()==0U" in r
assert "mdl->num_vertices=0;" in r
assert "?tb.size():tb.capacity())*3);" in r
assert "mdl->num_vertices = tb.size() * 3;" in r
print("Experimental compositor and native upload source contracts passed")
