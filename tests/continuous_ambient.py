"""CPU-only sparse continuous ambient portal stitching source contracts.

The prototype is intentionally not connected to the production renderer or
JSON settings. Native capture and visual integration require separate review.
"""
from pathlib import Path
root=Path(__file__).resolve().parents[1]
field=(root/"renderer/continuous_ambient.hpp").read_text(encoding="utf-8")
capture=(root/"renderer/continuous_ambient_capture.hpp").read_text(encoding="utf-8")
portal=(root/"renderer/world_vertex_ambient.hpp").read_text(encoding="utf-8")
test=(root/"tests/continuous_ambient.cpp").read_text(encoding="utf-8")
renderer=(root/"renderer/renderer.cpp").read_text(encoding="utf-8")
shader=(root/"assets/shaders/gpu_3do_preview.vert").read_text(encoding="utf-8")
config=(root/"common/config.hpp").read_text(encoding="utf-8")
workflow=(root/".github/workflows/main.yml").read_text(encoding="utf-8")

assert "class continuous_ambient_field {" in field
assert "std::unordered_map<node_key,accumulation,node_hash> accumulating_" in field
assert "std::unordered_map<node_key,node,node_hash> nodes_" in field
assert "std::unordered_map<node_key,std::vector<uint32_t>,node_hash> floor_owners_" in field
assert "max_nodes=2000000U" in field
assert "std::array<std::pair<node_key,double>,27> contributions{};" in field
assert "auto original=std::move(accumulating_);" in field
assert "raw_near(original,sector,group.first,light)" in field
assert "if(original.size()+stitched.size()+missing>max_nodes)continue;" in field
assert "triangle_distance2(location(at),p.vertices)" in field
assert "shared[at].push_back({p.a,p.b});" in field
assert "parent[root(b)]=root(a);" in field
assert "floor_owners_[key]=members;" in field
assert "float const raw=found!=nodes_.end()" in field
assert "? float(found->second.brightness)/255.0f*range_" in field
assert ": fallback->second.sample(location(at),range_);" in field
assert "for(uint32_t owner:owners->second)" in field
assert "lit=total/float(owners->second.size());" in field
assert "if(weights<=1e-12)return false;" in field
# Empty sector interiors fall back to eight sector-wide samples
# WITHOUT allocating a dense 3D array. Original sparse and stitched
# values always take precedence at their world-aligned grid positions.
assert "struct fallback_sector {" in field
assert "std::unordered_map<uint32_t,fallback_sector> fallback_" in field
assert "std::unordered_map<uint32_t,source_sector> authored_" in field
assert "fallback_.emplace(entry.first,output);" in field
assert "authored_.clear();" in field
assert "stats_.fallback_sectors=" in field
assert "fallback->second.sample(location(at),range_)" in field
assert "A huge open sector can have authored light" in test
assert "f.node_count()<=108U" in test
assert "return field.finalize();" in capture
assert "capture_world_vertex_ambient_portals(" in capture
assert "portal_geometry.for_each_portal_triangle(" in capture
assert "if(field.add_portal(a,b,triangle))" in capture
assert "clamp_authored_light(lights[i],overbright)" in capture
assert "range.passable_adjoin" in capture
assert "geo<3U || mode<3U" in capture
assert "for_each_portal_triangle(Visit &&visit)" in portal
assert "read(sector+0x08U" not in capture
assert "read(sector+0x04U" not in capture
assert "gl::" not in field and "glBind" not in field
assert '#include "continuous_ambient_capture.hpp"' in renderer
assert "void stage_continuous_ambient_cpu()" in renderer
assert "stage_continuous_ambient_cpu();" in renderer
assert "experimental_continuous_ambient_cpu" in renderer
assert "capture_continuous_ambient(" in renderer
assert "CONTINUOUS AMBIENT CPU snapshot world=" in renderer
assert "CONTINUOUS AMBIENT CPU coverage eligible=" in renderer
assert "inst.owning_sector_id,inst.owning_thing_world_position," in renderer
assert '#include "continuous_ambient_lookup.glsl"' in shader
assert "bool experimental_continuous_ambient_cpu = false;" in config
assert 'j.contains("experimental_continuous_ambient_cpu")' in (
    root/"common/config.cpp").read_text(encoding="utf-8")
assert '"experimental_continuous_ambient_cpu": false' in (
    root/"assets/jkgm.json").read_text(encoding="utf-8")
assert "| **experimental_continuous_ambient_cpu** |" in (
    root/"doc/config.md").read_text(encoding="utf-8")
native=(root/"renderer/main.cpp").read_text(encoding="utf-8")
assert "the_config->experimental_continuous_ambient_cpu" in native
assert "uintptr_t(hud_owner)+0xb8U" in native
assert "JKGM_NATIVE_CAPTURE_TEST" in test
assert "mem.put(adjoin,uint32_t(0));" in test
assert "mem.put(portal+0x10U,uint32_t(0x200U));" in test
assert "assert(near(a,b) && near(b,c));" in test
assert "Continuous volumetric ambient CPU portal-stitching prototype" in workflow
assert "tests\\continuous_ambient.cpp" in workflow
print("Continuous voxel ambient CPU topology source contracts passed")
