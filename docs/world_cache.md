# Cached SithWorld GPU rendering (experimental)

The opt-in cached-world renderer submits eligible SithWorld architecture through
persistent OpenGL geometry and a GPU-first opaque G-buffer pass. It is a
rendering path, **not** the old non-rendering `world_cache_probe` prototype.

Enable in `jkgm.json`:

```json
{
  "world_cache_preview": true,
  "profile_renderer": true
}
```

`world_cache_preview` defaults to `false`. It is independent of
`use_model_cache` (which controls 3DO mesh caching). Both can be enabled.
`profile_renderer` is optional and provides periodic WORLD CACHE statistics.

## Geometry and visibility

- The world cache retains validated static surface vertices, MAT UVs, flat
  surface normals and indexed triangles in GPU buffers. Surface IDs, native
  material pointers and candidate bounds remain associated with each range.
- Visibility/candidate staging uses SithWorld sector/adjoin information.
  Native sector visibility and HOM conventions remain important inputs.
- Live surface settings, sector tint, MAT/cel selection and lighting are
  sampled at render time rather than frozen into the static geometry.
- The cached pass currently targets supported opaque materials. Transparency,
  sky/portal geometry, special materials, animated materials and invalid
  resources are not generally handled by the opaque world cache.

**Caution:** The current GPU-first isolation path can suppress native
mixed world batches when a valid cached-world candidate set exists. Do not
assume every unsupported surface is automatically restored by a native
fallback: missing or duplicated surfaces remain parity-test targets. The
world renderer is still experimental.

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
Special surface paths and complete native visual parity are still work in
progress. Preserve an A/B test with `world_cache_preview=false` when
investigating missing faces, HOM or lighting differences.
