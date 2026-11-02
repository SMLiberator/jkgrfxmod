# GPU-cached SithWorld non-opaque drawing — first integration

The `world_cache_preview` path now classifies native world geometry by
**live SithSurface flags, GeoMode, MAT/cel and alpha properties**, while
retaining JK's verified sector/portal visibility and persistent GPU geometry.

## Supported first-stage world material paths

- Ordinary GeoMode 3 / 4 opaque: G-buffer depth and lighting.
- Explicit alpha-masked and dithered world MATs, including pass-through
  adjoins: alpha-tested depth and G-buffer silhouette.
- Mixed-alpha MAT: fully opaque texels write G-buffer depth; fractional
  texels blend later. Fully transparent texels render neither pass.
- Native `rdFace.type & 0x2`: forced `90/255` opacity, even with
  opaque MAT textures.
- Native material opacity below one: blended, without depth writes.
- Additive and multiplicative material blend modes: their appropriate
  framebuffer blend factors, rather than normal premultiplied blending.
- Sky types 0x200 / 0x400: retain their independent, in-game-confirmed
  projection/coverage path, not the ordinary translucent world path.

Opaque and transparent world passes use the same immutable VBO and the
same frame's uploaded animated UVs, baked/Gouraud lighting, sector tint,
and optional per-pixel point lights. The transparent pass uses sorted
cached surface ranges, with no depth writes. Its forward fragment
output is premultiplied; fog is applied there instead of in the
opaque full-screen composite.

## Intentionally deferred

Fluid thickness/refraction, MAT-to-MAT alpha transitions, and
emissive-only faces still use the native-material fallback policy.
Forward alpha does not yet receive world sunlight/spotlight shadows
or environment specularity with full opaque-surface parity.
Native world batches also contain fallback Things/sprites; isolation
still suppresses those mixed batches instead of preserving each
unsupported object individually. These are known migration gaps,
not reasons to recreate world geometry.

## In-game validation

Enable `world_cache_preview` and `profile_renderer`.
Check `WORLD CACHE gpu_first_opaque` for `mixed_opaque_ranges` and
`masked_ranges`, then `WORLD CACHE gpu_alpha` for `mixed` and
`forced_blend`. Inspect an opaque window with alpha-painted holes,
a native face-type 0x2 pane, additive/multiplicative surfaces, and
pass-through alpha-masked adjoins from either side. Compare depth
occlusion and the absence of black squares, double-painted pixels,
or objects disappearing through portals.

Native sorting across transparent *world versus Thing* groups is
not yet globally unified.
