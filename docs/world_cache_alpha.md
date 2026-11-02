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
  opaque MAT textures. Alpha coverage stays `90/255` while diffuse RGB
  follows the original D3D bridge's premultiply-**before**-gamma-1.8
  convention. Thus white diffuse RGB contributes `(90/255)^1.8`, while
  the framebuffer still blends using `90/255` alpha. This is not
  equivalent to multiplying already-linear RGB by `90/255`.
- Native material opacity below one: blended, without depth writes.
- Emissive maps, pulsing factors and scrolling; opaque emission writes
  the G-buffer emissive target after SSAO, and blended emission contributes
  independently of diffuse alpha.
- Emissive-only zero-alpha surfaces, including materials without albedo images.
- Animated albedo transitions (two MAT textures or fades to solid).
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

## Shared material adapter
Both GPU 3DO and world fragment shaders now include
`gpu_material_common.glsl` for MAT texture sampling, animated albedo
transitions, alpha-pass partition, Bayer dither and independent emission.
The separate vertex shaders, per-geometry lighting and sky projection remain
intact; specular, normal, environment and parallax code are not yet unified.

## Intentionally deferred

Fluid thickness/refraction and full 3DO-style specular/normal/environment
material evaluation still need the next shared-material expansion.
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
