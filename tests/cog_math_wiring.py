"""Verify COG InverseLerp argument order and registration (source-level)."""
from pathlib import Path

root=Path(__file__).resolve().parents[1]
source=(root/"renderer/main.cpp").read_text(encoding="utf-8")
helper=(root/"renderer/cog_math.hpp").read_text(encoding="utf-8")

start=source.index("void __cdecl cog_math_inverse_lerp(void *ctx)")
body=source[start:source.index("}",start)]
assert body.index("float const value=cog_pop_float(ctx);") < body.index(
    "float const b=cog_pop_float(ctx);") < body.index(
    "float const a=cog_pop_float(ctx);")
assert "cog_push_float(ctx,jkgm::cog_math_inverse_lerp(a,b,value));" in body
assert 'cog_verb_entry{"inverselerp", cog_math_inverse_lerp}' in source
assert "return a==b ? 0.0f : (value-a)/(b-a);" in helper
print("InverseLerp COG wiring and zero-interval guard checks passed")
