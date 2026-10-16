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
