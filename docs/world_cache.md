# Cached SithWorld GPU rendering

The unified GPU renderer submits SithWorld architecture, 3DO models and
ordinary sprites through their respective GPU caches. World geometry uses
persistent OpenGL buffers and a GPU-first G-buffer pass. It is a rendering
path, **not** the old non-rendering `world_cache_probe` prototype.

Enable in `jkgm.json`:

```json
{
  "use_gpu_cache": true,
  "profile_renderer": true
}
```

`use_gpu_cache` defaults to `true` and also controls GPU-cached 3DOs
and ordinary sprites. The retired per-path switches are ignored with a
warning. `profile_renderer` is optional and provides periodic WORLD CACHE
statistics. `enable_native_fallback=false` remains useful for visual
isolation tests; `enable_gpu_mat_warmup` is an independent experimental
opt-in option (default `false`).

## Geometry and visibility

- The world cache retains validated static surface vertices, MAT UVs, flat
  surface normals and indexed triangles in GPU buffers. Surface IDs, native
  material pointers and candidate bounds remain associated with each range.
- Visibility/candidate staging uses SithWorld sector/adjoin information.
  Native sector visibility and HOM conventions remain important inputs.
- Live surface settings, sector tint, MAT/cel selection and lighting are
  sampled at render time rather than frozen into the static geometry.
- Opaque, masked, translucent and special materials are split into the
  appropriate opaque and transparent passes. Sky, scrolling UVs and animated
  cels retain their dedicated rendering paths. Truly unreadable/unsupported
  native resources still require error handling or fallback.

**Caution:** With `enable_native_fallback=false`, native mixed world
batches are suppressed while a valid GPU world candidate set exists. That
also hides native fallback Things that happen to share the same batch.
Keep the default native fallback enabled unless explicitly testing GPU-only
coverage. Missing MATs can require `enable_gpu_mat_warmup`, which is still
experimental and disabled by default.

## Dynamic and baked lighting

`gpu_pixel_lighting` is the single opt-in pixel-lighting method switch for
**both** supported GPU-cached world and 3DO geometry:

- `false` (default): preserve JK-style vertex Gouraud lighting. Cached
  world faces use native JK's per-world-vertex dynamic-light buffer,
  combined with live authored surface lighting.
- `true`: interpolate the original baked world vertex RGB at rasterization
  and evaluate dynamic point lights per pixel, when a validated camera
  light list is available. A validated list with zero lights contributes
  zero rather than accidentally reusing stale vertex-light data.

The world and GPU-cached 3DO **pixel** paths use the editable formulas in
`assets/shaders/jk_point_lighting.glsl`. This source is included at shader
load time. Per-pixel dynamic light calculations do not replace authored
baked Gouraud colors; native non-cached geometry remains unaffected.

## Diagnostics and limitations

Set `profile_renderer` to inspect `WORLD CACHE` logs for current
candidate/draw counts, lighting fallback, GPU pixel-path validity,
and unresolved or special materials. No single counter proves pixel parity.

Static geometry caching assumes that in-place changes to world vertex
positions, UVs or topology are tracked or cause a suitable cache rebuild.
The ordinary cached rendering paths are now unified, but GPU-only coverage,
dynamic native material residency and device-reset behavior still deserve
regression testing. Set `use_gpu_cache=false` for a combined native
comparison when investigating HOM, lighting, or missing surfaces.


## Native cel animations and surface scrolling

JK retains complete ownership of `MaterialAnim()`, `SurfaceAnim()`,
animated sprite frame progression, and `SlideSurface()` timing/IDs.
GPU rendering reads live native cels and scroll offsets; it never
advances a duplicate animation timer.

A surface cel of `-1` follows `rdMaterial.curCelNum`; explicit face
cels override it. Cached world draw candidates query the live cel every
frame, just as GPU-cached 3DOs and sprites use their own native cel
selection.

`SlideSurface()` updates native `rdFace.texVertOffset`, stored at
SithSurface offsets `+0x3c/+0x40` in the supported JK executable.
World-candidate staging now captures those two offsets together with
the live cel/material/GeoMode. A small streaming vec2 attribute passes
the offsets to the world vertex shader; authored positions, UVs and
index buffers remain immutable. The native offset is added in MAT
pixel coordinates before normalization by the current cel dimensions.
Material-pack scrolling remains separate.

This does not change translucent-world eligibility or force native MAT
textures to be uploaded. Frames missing from JK's existing texture
cache can still be unavailable to GPU world rendering; resolving
native cache residency and mixed-batch fallback is separate work.
