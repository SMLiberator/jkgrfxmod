"""Identical native opaque/transparent helpers must come from one GLSL include.

This guards source equivalence without changing native material or sun policy.
The actual OpenGL shader compilation still happens at game runtime.
"""
from pathlib import Path

base = Path(__file__).resolve().parents[1] / "assets" / "shaders"
shared = (base / "game_surface_common.glsl").read_text(encoding="utf-8")
include = '#include "game_surface_common.glsl"'
signatures = (
    "vec3 surface_view_normal(",
    "vec3 compute_reflection(",
    "mat3 construct_tbn(",
    "float dither_threshold(",
)
assert "#version" not in shared
for signature in signatures:
    assert shared.count(signature) == 1, signature
for filename in ("game_opaque_pass.frag", "game_trns_pass.frag"):
    source = (base / filename).read_text(encoding="utf-8")
    assert source.startswith("#version 330 core"), filename
    assert source.count(include) == 1, filename
    for signature in signatures:
        assert signature not in source, (filename, signature)
    assert source.index("uniform vec4 normal_params;") < source.index(include)
    expanded = source.replace(include, shared)
    for signature in signatures:
        assert expanded.count(signature) == 1, (filename, signature)
    assert expanded.index("mat3 construct_tbn(") < expanded.index("parallax_mapping(")
    assert expanded.index("float dither_threshold(") < expanded.index("void main(")
print("Native game-pass shared GLSL helpers and declaration ordering OK")
