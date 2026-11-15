"""Source contracts for all 3 DirectDraw HUD update modes."""
from pathlib import Path
import json

root=Path(__file__).resolve().parents[1]
render=(root/"renderer/renderer.cpp").read_text(encoding="utf-8")
config=(root/"common/config.hpp").read_text(encoding="utf-8")
parser=(root/"common/config.cpp").read_text(encoding="utf-8")
sample=json.loads((root/"assets/jkgm.json").read_text(encoding="utf-8"))
header=(root/"renderer/hud_dirty_regions.hpp").read_text(encoding="utf-8")
assert sample["experimental_hud_updates"]==0
assert "int experimental_hud_updates = 0;" in config
assert 'j.contains("experimental_hud_updates")' in parser
assert "experimental_hud_updates must be 0, 1 or 2" in parser
assert '#include "hud_dirty_regions.hpp"' in render
assert "find_hud_dirty_regions(" in render
assert "std::memcmp(a,b," in header
assert "out.rects.size()>max_regions" in header
assert "hud_previous_source.clear();" in render
assert "hud_previous_color_key!=key" in render
assert "std::memcpy(hud_previous_source.data()+src_row+rect.left" in render
assert "std::fill(source.begin(),source.end(),key);" in render
assert "if(the_config->experimental_hud_updates==0)" in render
start=render.index("void end_frame()")
end=render.index("void update_hud_texture_live()")
present=render[start:end]
assert present.index("update_hud_texture_live();") < present.index("draw_hud();")
assert "profile_idx(profile_stage::hud_upload)" in present
assert "PERF HUD_updates mode=" in render
print("DirectDraw HUD update modes and presentation ordering OK")
