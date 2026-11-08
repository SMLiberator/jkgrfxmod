"""Sun shadow strength follows light intensity on every receiver path.

No OpenGL context is required. Keep the full-intensity appearance while
preventing tiny positive intensities from darkening existing baked lighting.
"""
from math import isclose
from pathlib import Path

root = Path(__file__).resolve().parents[1]
shaders = {
    "opaque": (root / "assets/shaders/post_opaque_composite.frag").read_text(encoding="utf-8"),
    "native_alpha": (root / "assets/shaders/game_trns_pass.frag").read_text(encoding="utf-8"),
    "gpu_alpha": (root / "assets/shaders/gpu_3do_preview.frag").read_text(encoding="utf-8"),
}
opaque = shaders["opaque"]
assert "sun_shadow_color_intensity.a,0.0,1.0" in opaque
assert "0.6*sun_applied)*" in opaque
assert "sun_shadow_strength;" in opaque
assert "surface_rgb*=1.0-shadowed_baseline*(1.0-direct_visibility);" in opaque

for name in ("native_alpha", "gpu_alpha"):
    shader = shaders[name]
    assert "clamp(translucent_sun_color.a,0.0,1.0)" in shader, name
    if name == "gpu_alpha":
        assert "0.75*range_fade*sun_shadow_strength*" in shader
    else:
        assert "0.75*range_fade*clamp(translucent_sun_color.a,0.0,1.0)" in shader
    # A closed sunward ray is now equivalent to a shadow for both the
    # developer's direct sunlight and the baked-baseline contrast.
    assert "translucent_sun_color.a*incidence*direct_visibility*opacity*range_fade;" in shader
    assert "float sky_access=translucent_sun_sky_aperture(world);" in shader
    assert "float direct_visibility=visibility*sky_access;" in shader
    assert "if(sky_access<=0.0)return baked;" not in shader

def shade(base, strength, visibility, multiplier):
    return base*(1.0-multiplier*min(max(strength,0.0),1.0)*(1.0-visibility))

for intensity in (0.0, 0.001, 0.01, 0.1, 0.5, 1.0, 2.0, 16.0):
    for multiplier in (0.6, 0.75):
        unshadowed = shade(0.8,intensity,1.0,multiplier)
        shadowed = shade(0.8,intensity,0.0,multiplier)
        assert isclose(unshadowed,0.8)
        assert 0.0 <= shadowed <= unshadowed
        if intensity == 0.001:
            assert isclose((unshadowed-shadowed)/0.8,multiplier*0.001)
        if intensity >= 1.0:
            assert isclose(shadowed,0.8*(1.0-multiplier))

print("Sun shadow strength follows sunlight intensity on all receivers")
