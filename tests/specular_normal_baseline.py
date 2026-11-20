"""The abandoned specular-normal A/B experiment is fully removed."""
from pathlib import Path

root=Path(__file__).resolve().parents[1]
read=lambda p:(root/p).read_text(encoding="utf-8")
for p in ("common/config.hpp","common/config.cpp","assets/jkgm.json",
          "doc/config.md","renderer/renderer.cpp",
          "renderer/shaders.hpp","renderer/shaders.cpp"):
    assert "specular_normal_mode" not in read(p),p
for p,count,needle in (
    ("assets/shaders/gpu_3do_preview.frag",2,"if(dot(N,V)<0.0) N=-N;"),
    ("assets/shaders/world_cache_direct_specular.glsl",1,"if(dot(N,V)<0.0)N=-N;"),
    ("assets/shaders/game_opaque_pass.frag",1,"if(dot(N, V) < 0.0) N = -N;"),
):
    shader=read(p)
    assert "experimental_specular_normal_mode" not in shader,p
    assert "gl_FrontFacing" not in shader or "experimental_specular_normal_mode" not in shader,p
    assert shader.count(needle)==count,(p,shader.count(needle))
print("Standard original specular normal behavior contracts passed")
