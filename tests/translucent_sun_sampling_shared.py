"""The native/GPU alpha paths reuse identical directional sun map sampling.

Their visibility/atlas-selection logic is intentionally DIFFERENT and must
remain in the owning shader until its behavior is explicitly reconciled.
"""
from pathlib import Path
base=Path(__file__).resolve().parents[1]/"assets"/"shaders"
common=(base/"translucent_sun_sampling.glsl").read_text(encoding="utf-8")
include='#include "translucent_sun_sampling.glsl"'
for filename in ("game_opaque_pass.frag","game_trns_pass.frag","gpu_3do_preview.frag"):
    source=(base/filename).read_text(encoding="utf-8")
    assert source.count(include)==1,filename
    for signature in ("vec3 translucent_sun_coordinates(",
                      "bool translucent_sun_inside(",
                      "float translucent_sun_sample("):
        assert signature not in source,(filename,signature)
        assert common.count(signature)==1,(filename,signature)
    assert "float translucent_sun_visibility(" in source,filename
    if filename!="game_opaque_pass.frag":
        assert "vec3 illuminate_translucent_sun(" in source,filename
print("Shared translucent sun sampling GLSL parity OK")
