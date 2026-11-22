"""Phase B: opaque geometry order is cached; alpha and sprites retain parity."""
from pathlib import Path

root=Path(__file__).resolve().parents[1]
header=(root/"renderer/gpu_3do_preview.hpp").read_text(encoding="utf-8")
render=(root/"renderer/renderer.cpp").read_text(encoding="utf-8")
draw=render.split("void draw_gpu_3do_preview(",1)[1].split(
    "// Deliberate world-cache isolation test.",1)[0]

assert "cached_opaque_group_order() const" in header
assert "std::call_once(opaque_order_once" in header
assert "return a<b; // identical keys retain authored order" in header
for field in ("material","cel","face_type","nonplanar","solid_color"):
    assert f"lhs.{field}!=rhs.{field}" in header
assert "for(size_t group_visit=0;group_visit<groups.size();++group_visit)" in draw
assert "auto const *opaque_order=transparent ? nullptr" in draw
assert "&inst.geometry->cached_opaque_group_order()" in draw
assert "size_t const group_index=opaque_order" in draw
assert "auto const &group=groups[group_index];" in draw
assert "if(opaque_order) next_draw_order+=groups.size();" in draw
assert "instance_order_base+group_index:next_draw_order++" in draw
assert "ordinary_alpha_draws.push_back(candidate);" in draw
assert draw.count("std::sort(draws.begin(),draws.end(),")==1
opaque=draw.split("if(!transparent) {",1)[1].split(
    "if(transparent && !(post_exempt",1)[0]
assert "std::sort(draws.begin(),draws.end()," not in opaque
assert "sprite_buckets[index].first_order=candidate.order;" in draw
assert "sprite_buckets_in_source_order=false;" in draw
assert "if(!sprite_buckets_in_source_order)" in draw
assert "return a.first_order<b.first_order;" in draw
assert "merged_draws.insert(merged_draws.end()," in draw
assert "if(a.depth==b.depth) return a.order<b.order;" in draw
assert "return a.depth>b.depth;" in draw
assert "gpu_3do_trail_alpha_mode(" in draw
assert "candidate.group->saber_trail_tip_bridge" in draw
print("GPU 3DO cached opaque order, sprites, and alpha parity wiring OK")
