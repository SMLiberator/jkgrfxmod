# JkGRFxMod

A modification of JkGfxMod specifically built for JkGR

## Features

- Cog controllable distance fog, includes new Cog verbs
- SSAO fades with distance so it no longer affects skyboxes
- Other Cog controllable VFX such as film grain, vignette and color filters


## VFX configuration (`jkgm.json`)

Optional boolean master switches, all `true` by default:

| Setting | Effect |
| --- | --- |
| `enable_fog` | Renders COG-controlled distance fog; COG parameters are still stored if disabled |
| `enable_reflections` | Enables material environment reflections (including fixed/MatCap and the planned SSR); disabling restores the unreflected diffuse appearance |
| `enable_post` | Enables optional presentation effects (bloom, film grain, vignette, brightness/contrast/saturation); sRGB output remains active when disabled |
| `enable_blur` | Reserved for frame blending and future spatial blur effects; no blur implementation is included in this change |

The existing `enable_ssao` switch remains independent of `enable_post`, making it useful for testing SSAO artifacts. `enable_bloom` independently disables bloom even when `enable_post` is true.

### Fog COG changes

Fog no longer has separate enabled state. `SetFogInfo(vector(start, end, opacity), fadeTime)` uses opacity alone, where `0` disables fog. `GetFogInfo()`, `SetFogColor()`, `GetFogColor()`, and `ResetFog()` remain. The old `SetFogEnabled` and `GetFogEnabled` verbs have been removed; existing COGs using those verbs must be updated.

The default fog parameters are `40 1000 0` (off). Setting opacity nonzero fades fog in according to `fadeTime`.


## SSAO diagnostic views (temporary investigation branch)

Add `"debug_render_mode"` to `jkgm.json`. Accepted values:

- `"none"`: normal gameplay
- `"normals"`: view-space G-buffer normals mapped to RGB (0.5 means zero); magenta means missing/invalid geometry
- `"depth"`: linear view depth, black = near and white = 128+ units; magenta means no geometry
- `"ssao_raw"`: raw SSAO before edge-aware blur, black = no occlusion, white = maximum occlusion
- `"ssao"`: SSAO after blur, same grayscale meaning

For clean comparisons, debug views automatically bypass bloom, vignette, film grain, brightness/contrast/saturation, transparent geometry, and the HUD. sRGB display conversion stays active. The views intentionally show only opaque G-buffer geometry.

For each mode, take a screenshot from the **same camera position**, especially at a spot where unwanted dark patches appear. Compare `ssao_raw` with `ssao` to distinguish sampling errors from blur bleeding. The diagnostic requires `"enable_ssao":true`. Compare `antialiasing` MSAA enabled/disabled independently.

The diagnostic build is not intended for normal play; set `"debug_render_mode":"none"` to return to normal operation.


### Native versus geometric normal isolation

Set `"debug_force_face_normals":true` to bypass the smooth vertex normals
supplied by the patched JK executable and use a per-triangle normal computed
from the rendered triangle positions instead. It affects all drawn meshes,
independently of whether they have textures or vertex colors.

Try this on the two imported levels where `debug_render_mode="normals"`
and `"ssao_raw"` show discontinuities:

1. `"debug_force_face_normals":false` uses the usual native normal source.
2. `"debug_force_face_normals":true` forces geometric face normals.
3. Compare the same stationary camera views in `"normals"` and `"ssao_raw"`.
4. Expect visible hard triangle edges with face normals; that's normal. Look
   instead for **unexpected** changes *within planar surfaces* or stray AO patches.

If unexpected artifacts disappear, investigate the source 3DO normals or the
patched executable's native-normal validity/packing. If both modes look broken,
investigate surface triangulation, winding, degeneracy and geometric normal
reconstruction. The setting is off by default and is diagnostic only.


### Test native-normal slot reuse (experimental)

The JK patch publishes view normals in a sideband array indexed by D3D transformed
vertex slots. With `"debug_consume_native_normals":true`, the renderer clears
**only the validity bit** of each entry in the execute buffer's vertex range
AFTER consuming all triangles in that buffer. The setting is diagnostic, not
a permanent solution, and is off by default.

In a level that flickers in **Solid** graphics mode, test:
- `"debug_render_mode":"normals"`
- `"debug_force_face_normals":false`
- Compare `"debug_consume_native_normals":false` vs `true`

If flicker disappears, native-normal entries from prior execute buffers were being
mistaken for current normals. If it remains, inspect native normal writes and
index correspondence inside the patched executable (including Solid and
clipping paths) rather than assuming the source 3DO normals are wrong.
