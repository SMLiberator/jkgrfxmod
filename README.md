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
| `enable_blur` | Enables recursive frame blending and future spatial blur effects; independent of `enable_post` |

The existing `enable_ssao` switch remains independent of `enable_post`, making it useful for testing SSAO artifacts. `enable_bloom` independently disables bloom even when `enable_post` is true.

### Fog COG changes

Fog no longer has separate enabled state. `SetFogInfo(vector(start, end, opacity), fadeTime)` uses opacity alone, where `0` disables fog. `GetFogInfo()`, `SetFogColor()`, `GetFogColor()`, and `ResetFog()` remain. The old `SetFogEnabled` and `GetFogEnabled` verbs have been removed; existing COGs using those verbs must be updated.

The default fog parameters are `40 1000 0` (off). Setting opacity nonzero fades fog in according to `fadeTime`.


## COG-controlled frame blending

Temporal accumulation is applied to the **finished composited frame** (including
gameplay, HUD, bloom, film grain and color adjustments) in linear color space,
with hardware sRGB output conversion preserved. It needs no changes to
`jk.exe`; the verbs are registered through the existing community patch `JK.dll`
hook.

```c
SetFrameBlend(0.75, 0.5); // Fade to 75% previous accumulated output over 0.5 seconds.
flex amount = GetFrameBlend(); // Current interpolated strength, 0.0..1.0.
SetFrameBlend(1.0, 0.0); // Freeze the last completed frame.
SetFrameBlend(0.0, 0.5); // Fade back to live gameplay.
ResetFrameBlend();       // Disable blending AND discard history immediately.
```

The accumulation formula is `output = current * (1 - strength) + previous * strength`,
where `previous` is the last **blended output**, not the last raw frame.
Strength `0` means no trails; values close to `1` yield a long afterimage;
`1` holds the last displayed frame. Defaults to `0`. A fade always begins
at the presently interpolated value; `fadeTime <= 0` updates immediately.

History is continuously refreshed even at strength 0, so turning on blending
does not show a stale old frame. It is invalidated on menu/game rendering-mode
transitions and when `ResetFrameBlend()` is called. These transitions also reset
the effect's strength to zero to avoid carrying a level effect into the menus.
The engine's per-frame `BeginScene`/`EndScene` hooks do not reset it.

`"enable_blur":false` prevents the history contribution while keeping the
COG-controlled parameters intact; use this for debugging. It does not turn
off bloom or SSAO. Frame blending is also independent of `enable_post`, which
only gates bloom, grain, vignette and color filtering.

**Known limitation:** A direct in-game level-to-level transition that never
enters menu rendering does not currently trigger an automatic reset. Such
transitions should call `ResetFrameBlend()` from a shutdown/startup COG.
The explicit history reset is also useful before cutscene camera teleports.
