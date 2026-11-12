"""Verify the opt-in specular normal comparison stays isolated and reversible."""
from pathlib import Path
import json
root=Path(__file__).resolve().parents[1]
read=lambda p:(root/p).read_text(encoding="utf-8")
config=json.loads(read("assets/jkgm.json"))
assert config["experimental_specular_normal_mode"]==0
assert 'int experimental_specular_normal_mode = 0' in read("common/config.hpp")
parser=read("common/config.cpp")
assert "experimental_specular_normal_mode must be 0, 1 or 2" in parser
sources=[
    ("assets/shaders/gpu_3do_preview.frag",2),
    ("assets/shaders/world_cache_direct_specular.glsl",1),
    ("assets/shaders/game_opaque_pass.frag",1),
]
for path,count in sources:
    src=read(path)
    assert "uniform int experimental_specular_normal_mode;" in src,path
    assert src.count("experimental_specular_normal_mode==0")==count,path
    assert src.count("experimental_specular_normal_mode==2 && !gl_FrontFacing")==count,path
    assert "if(dot(N" in src,path
shader=read("renderer/shaders.cpp")
hdr=read("renderer/shaders.hpp")
renderer=read("renderer/renderer.cpp")
for cls in ("world_cache_preview_shader","gpu_3do_preview_shader","game_pass_shader"):
    assert f"{cls}::set_specular_normal_mode(" in shader
assert hdr.count("void set_specular_normal_mode(int mode);")==3
assert renderer.count("the_config->experimental_specular_normal_mode")==4
assert 'gl::set_uniform_integer(specular_normal_mode,mode)' in shader
assert "experimental_specular_normal_mode" not in read("assets/shaders/game_transparency_pass.frag") if (root/"assets/shaders/game_transparency_pass.frag").exists() else True
print("specular normal-mode A/B source contracts OK")
