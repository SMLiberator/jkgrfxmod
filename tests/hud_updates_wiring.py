"""Source contracts for standard adaptive DirectDraw HUD updates."""
from pathlib import Path
import json

root=Path(__file__).resolve().parents[1]
render=(root/"renderer/renderer.cpp").read_text(encoding="utf-8")
config=(root/"common/config.hpp").read_text(encoding="utf-8")
parser=(root/"common/config.cpp").read_text(encoding="utf-8")
sample=json.loads((root/"assets/jkgm.json").read_text(encoding="utf-8"))
header=(root/"renderer/hud_dirty_regions.hpp").read_text(encoding="utf-8")
assert "experimental_hud_updates" not in sample
assert "experimental_hud_updates" not in config
assert "experimental_hud_updates" not in parser
assert "experimental_hud_updates" not in render
assert '#include "hud_dirty_regions.hpp"' in render
assert "find_hud_dirty_regions(" in render
assert "std::memcmp(a,b," in header
assert "out.rects.size()>max_regions" in header
assert "hud_previous_source.clear();" in render
assert "hud_previous_color_key!=key" in render
assert "std::memcpy(hud_previous_source.data()+src_row+rect.left" in render
assert "std::fill(source.begin(),source.end(),key);" in render
assert "void update_hud_texture()" not in render
assert "bool const force_full=hud_previous_source.size()!=source.size()" in render
start=render.index("void end_frame()")
end=render.index("void update_hud_texture_live()")
present=render[start:end]
assert present.index("update_hud_texture_live();") < present.index("draw_hud();")
assert "profile_idx(profile_stage::hud_upload)" in present
assert "PERF HUD_dirty_regions bytes_uploaded_avg=" in render
assert "PERF HUD_updates mode=" not in render
print("Adaptive DirectDraw HUD and presentation ordering OK")
