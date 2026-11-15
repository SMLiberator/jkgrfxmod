"""Guard against top-console HUD magnification unrelated to hud_scale.

The HUD atlas places two 64-pixel corner regions beside its central console
strip. Its on-screen console quad must use the same source width (W - 128)
before applying the user-specified overall HUD scale.
"""
from pathlib import Path

source = (Path(__file__).resolve().parents[1] / "renderer/opengl_state.cpp").read_text(encoding="utf-8")

# Console source and destination must agree before applying HUD scale.
assert "auto console_tc = make_box(make_point(64, 0), make_size(get<x>(int_scr_res) - 128, 64));" in source
assert "auto con_sz = make_size(get<x>(int_scr_res_f) - 128.0f, 64.0f) * scale;" in source
assert "auto con_off = (get<x>(int_scr_res_f) - get<x>(con_sz)) * 0.5f;" in source
assert "make_point(con_off, get<y>(int_scr_res_f) - get<y>(con_sz))" in source

# The rest of the HUD should retain its existing, independent layout.
assert "make_size(get<x>(int_scr_res_f), get<y>(int_scr_res_f) - 128.0f)" in source
assert "auto sel_sz = make_size(get<x>(int_scr_res_f) - 128.0f, 64.0f) * scale;" in source

for width in (640, 800, 1024, 1920):
    for hud_scale in (1.0, 1.5, 2.0):
        source_width = width - 128
        console_width = source_width * hud_scale
        console_left = (width - console_width) * 0.5
        assert abs(console_width / source_width - hud_scale) < 1e-7
        if hud_scale == 1.0:
            assert console_left == 64

print("HUD console strip scales uniformly with other HUD regions")
