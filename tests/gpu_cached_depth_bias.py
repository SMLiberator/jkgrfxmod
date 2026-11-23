"""GPU-cached MAT depth bias must match native polygon offset.

Source-level integration checks complement the Windows renderer build: they
ensure the cached material carries the same depth-bias value, all forward and
reflection passes apply it per draw, and polygon-offset state never leaks.
"""
from pathlib import Path
import re

root = Path(__file__).resolve().parents[1]
renderer = (root / "renderer/renderer.cpp").read_text(encoding="utf-8")
probes = (root / "renderer/hemi_reflection_capture.inc").read_text(
    encoding="utf-8"
)
compact = lambda text: re.sub(r"\s+", "", text)

def section(text, begin, end):
    start = text.index(begin)
    return text[start:text.index(end, start + len(begin))]

helper = section(renderer, "class gpu_cached_polygon_offset {",
                 "class renderer_impl : public renderer")
cached_mat = section(renderer, "struct gpu_3do_texture {",
                     "class gpu_cached_polygon_offset {")
solid = section(renderer, "gpu_3do_texture resolve_gpu_3do_solid_material(",
                "gpu_3do_texture resolve_gpu_3do_material(")
textured = section(renderer, "gpu_3do_texture resolve_gpu_3do_material(",
                   "std::pair<float,float> gpu_specular_parameters(")
models = section(renderer, "void draw_gpu_3do_preview(",
                 "// Deliberate world-cache isolation test.")
world = section(renderer, "void draw_world_cache_preview(",
                "#include \"screen_space_reflections_renderer.inc\"")
alpha = section(renderer, "void draw_world_cache_transparency(",
                "void draw_game_transparency_pass(")
planar = section(renderer, "void render_planar_reflection_capture(",
                 '#include "hemi_reflection_capture.inc"') if (
                     "void render_planar_reflection_capture(" in renderer
                 ) else section(renderer, "gpu_cached_polygon_offset capture_depth_offset(",
                                '#include "hemi_reflection_capture.inc"')

# Preserve the native sign convention and every caller's optional base offset.
assert "float depth_bias=0.0f;" in compact(cached_mat)
assert "out.depth_bias=surface->depth_bias;" in compact(textured)
assert "out.depth_bias=vidmem_texture_surfaces[id-1]->depth_bias;" in compact(solid)
h = compact(helper)
assert "base_factor-bias" in h and "base_units-bias" in h
assert "glPolygonOffset(factor,units)" in h
assert "if(!std::isfinite(bias))bias=0.0f;" in h
assert "if(factor==0.0f&&units==0.0f)" in h
assert "glDisable(GL_POLYGON_OFFSET_FILL)" in h
assert "~gpu_cached_polygon_offset()" in h

# Both GPU-cached opaque/alpha geometry and the original decal preference.
m = compact(models)
assert "decals_only?-1.0f:0.0f,decals_only?-2.0f:0.0f" in m
assert "material_depth_offset.set(post_exempt?0.0f:material.depth_bias)" in m
assert "material_depth_offset.reset()" in m
w = compact(world)
assert "isolate_native_world_batches()?0.0f:-2.0f" in w
assert "material_depth_offset.set(material.depth_bias)" in w
assert "glMultiDrawElements(" in world
a = compact(alpha)
assert "gpu_cached_polygon_offsetmaterial_depth_offset(0.0f,0.0f)" in a
assert "material_depth_offset.set(material.depth_bias)" in a

# Reflection cameras use their own depth target but the same MAT preference.
p = compact(planar)
assert "gpu_cached_polygon_offsetcapture_depth_offset(0.0f,0.0f)" in p
assert "capture_depth_offset.set(mat.depth_bias)" in p
q = compact(probes)
assert "gpu_cached_polygon_offsetcapture_depth_offset(0.0f,0.0f)" in q
assert "capture_depth_offset.set(mat.depth_bias)" in q
assert q.index("capture_depth_offset.set(mat.depth_bias)") < q.index(
    "if(last_submitted_material&&*last_submitted_material==binding)returntrue"
)

# Pixel/linear-depth shader contracts are unchanged by polygon offset.
preview = (root / "assets/shaders/gpu_3do_preview.frag").read_text(
    encoding="utf-8"
)
cached_world = (root / "assets/shaders/world_cache.frag").read_text(
    encoding="utf-8"
)
assert "out_depth=vec4(view_depth," in compact(preview)
assert "out_depth=vec4(32.0/max(gl_FragCoord.w" in compact(cached_world)

# Positive bias moves near in both native and GPU paths; fallback offsets
# remain their authored defaults when depth_bias is zero.
for bias in (-3.0, -0.5, 0.0, 0.5, 3.0):
    for base in (0.0, -2.0):
        factor, units = base-bias, base-bias
        assert factor == units
        if bias > 0:
            assert factor < base and units < base
        elif bias < 0:
            assert factor > base and units > base
        else:
            assert factor == base and units == base
print("GPU cached material depth bias forward/reflection contracts passed")
