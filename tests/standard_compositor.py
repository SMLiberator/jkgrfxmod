"""Source contracts for standard fused bloom and optimized native uploads."""
from pathlib import Path

root=Path(__file__).resolve().parents[1]
read=lambda p:(root/p).read_text(encoding="utf-8")
cfg=read("common/config.hpp")
parser=read("common/config.cpp")
doc=read("doc/config.md")
sample=read("assets/jkgm.json")
state=read("renderer/opengl_state.hpp")
ctor=read("renderer/opengl_state.cpp")
bloom=read("renderer/renderer_compositor_bloom.cpp")
renderer=read("renderer/renderer.cpp")
scale=read("assets/shaders/post_scale.frag")
shaders=read("renderer/shaders.cpp")
for key in ("experimental_fused_bloom_downsample",
            "experimental_skip_bloom_clears",
            "experimental_native_triangle_uploads"):
    for source in (cfg,parser,doc,sample,state,ctor,renderer,bloom):
        assert key not in source, key
assert "bool const full_viewport=glIsEnabled(GL_SCISSOR_TEST)==GL_FALSE;" in bloom
assert "bool const fused_bloom=full_viewport;" in bloom
assert "bool const skip_bloom_clears=full_viewport;" in bloom
assert "if(!fused_bloom) {" in bloom  # scissor fallback
assert bloom.count("if(!skip_bloom_clears)") == 4
assert "if(fused_bloom || skip_bloom_clears)" in bloom
assert "gl::disable(gl::capability::blend);" in bloom
assert "gl::disable(gl::capability::depth_test);" in bloom
assert "gl::clear({gl::clear_flag::color" in bloom
assert "set_fused_bloom(" in shaders
assert "if(fused_bloom_first_pass!=0)" in scale
assert "if(tb.size()==0U)" in renderer
assert "mdl->num_vertices=0;" in renderer
assert "tb.size()*3" in renderer
assert "tb.capacity())*3" not in renderer
print("Standard compositor and used-count native upload contracts passed")
