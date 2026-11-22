"""Phase B: opaque geometry order is cached; alpha and sprites retain parity."""
from pathlib import Path

root=Path(__file__).resolve().parents[1]
header=(root/"renderer/gpu_3do_preview.hpp").read_text(encoding="utf-8")
render=(root/"renderer/renderer.cpp").read_text(encoding="utf-8")
draw=render.split("void draw_gpu_3do_preview(",1)[1].split(
    "// Deliberate world-cache isolation test.",1)[0]

assert "cached_opaque_group_order() const" in header
assert "std::call_once(cache.once" in header
assert "struct opaque_order_cache_owner {" in header
assert "opaque_order_cache_owner(opaque_order_cache_owner const &)" in header
assert "opaque_order_cache_owner &operator=(" in header
assert "std::make_shared<opaque_order_cache>()" in header
assert "mutable opaque_order_cache_owner opaque_order_state;" in header
assert "mutable std::once_flag opaque_order_once;" not in header
assert "return a<b; // identical keys retain authored order" in header
# Opaque sorting now uses the same immutable submission metadata as
# reflections. Its cached tuple preserves the original comparison order.
assert "meta.opaque_sort_key=std::make_tuple(" in header
opaque_fields=header.split("meta.opaque_sort_key=std::make_tuple(",1)[1].split(");",1)[0]
fields=("group.material","group.cel","group.face_type",
        "group.nonplanar","group.solid_color")
positions=[opaque_fields.index(field) for field in fields]
assert positions==sorted(positions)
assert "meta[a].opaque_sort_key!=meta[b].opaque_sort_key" in header
assert "return meta[a].opaque_sort_key<" in header
assert "auto const &submission=inst.geometry->cached_group_submissions();" in draw
assert "meta.main_material_key |" in draw
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
