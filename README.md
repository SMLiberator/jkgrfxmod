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
| `enable_blur` | Enables distance and peripheral spatial blur (more GPU-intensive) |
| `enable_trails` | Enables lightweight temporal feedback / frame blending |

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

`"enable_trails":false` prevents the history contribution while keeping the
COG-controlled parameters intact; use this for debugging. It does not turn
off bloom or SSAO. Frame blending is also independent of `enable_post`, which
only gates bloom, grain, vignette and color filtering.

**Known limitation:** A direct in-game level-to-level transition that never
enters menu rendering does not currently trigger an automatic reset. Such
transitions should call `ResetFrameBlend()` from a shutdown/startup COG.
The explicit history reset is also useful before cutscene camera teleports.


## COG-controlled distance and peripheral blur

These spatial effects are **disabled by default**. They are evaluated on
the full-resolution game scene in linear HDR before bloom, presentation
color processing, and recursive frame blending. Both are independently
fadeable and can be combined; `enable_blur:false` bypasses both
spatial effects without disabling frame blending.

```c
// Vector = startDistance endDistance strength, fadeTime in seconds.
SetDistanceBlur(VectorSet(40, 100, 0.8), 1.0);
vector currentDistance = GetDistanceBlur();
ResetDistanceBlur();

// Vector = innerRadius outerRadius strength, fadeTime in seconds.
// Radii 0..1 are measured from screen center to the corners.
SetPeripheralBlur(VectorSet(0.45, 0.9, 0.7), 0.5);
vector currentPeripheral = GetPeripheralBlur();
ResetPeripheralBlur();
```

Both strengths are clamped to `[0,1]`. Zero disables that effect.
Distance thresholds are JK engine units (not pixels). The distance
intensity ramps from 0 at the start to full strength at the end;
pixels without valid depth do not receive *distance* blur. Peripheral
blur is radial, with 0 at the center and 1 at the far corners.
All values animate over the requested `fadeTime`, using the same
wall-clock mechanism as the other COG effects.

The implementation uses horizontal and vertical 9-tap Gaussian passes.
Maximum blur extends about 12 screen pixels per axis at strength 1;
the radius grows smoothly as the amount grows. Distance blur prefers
samples of similar depth to reduce foreground/background bleed.
Peripheral blur deliberately can cross depth boundaries.

These effects use the existing opaque G-buffer depth. Consequently,
they operate on the finished scene image and can also blur HUD and
transparent objects lying over distant geometry; blur deliberately
remains in screen space. They do not alter the source 3DOs, skybox
geometry, or SSAO. The currently displayed parameters reset to off
when switching between menu and gameplay. For a direct game-to-game
level change without a menu transition, call both Reset* verbs from a COG
if level-specific effects must not persist.


### Blur performance switches

Only two independent configuration switches are needed:

```json
{
  "enable_blur": false,
  "enable_trails": true
}
```

`enable_blur` controls both spatial blur types (distance and peripheral).
`enable_trails` controls recursive temporal feedback from `SetFrameBlend`.
Both default to `true`, but all individual effect strengths default to zero.

With `enable_blur:false`, the expensive spatial sampling passes are skipped
even if COG has set nonzero distance/peripheral strengths; temporal trails still
work. With `enable_trails:false`, history continues updating from the live
frame without making a contribution, so re-enabling it does not revive an
old image. The matching COG verbs remain available regardless of either switch.


## Material blend modes

Material packs now support an optional `blend_mode`, independent of the
existing `alpha_mode` (blend/mask). Valid choices:
- `normal` (default): exactly the existing renderer blending behavior
- `additive`: add colored light to the scene, useful for flares/fire/glows
- `multiply`: multiply underlying scene color by a texture-dependent
  factor, useful for blood/dirt/stains

These can be specified on an individual material **or** as a pack-wide
default. The compiler validates each mode and writes it to the generated
`metadata.json`, which the runtime reads when loading replacement materials.

Example input script material:

```json
{
  "name": "blood_pool",
  "albedo_map": "blood.png",
  "alpha_mode": "blend",
  "blend_mode": "multiply",
  "replaces": ["blood.mat"]
}
```

The alpha channel controls the strength of the new modes. **Additive** adds
the lit material's color, scaled by texture/material alpha; an alpha-zero
texel contributes nothing. **Multiply** uses the opacity-weighted factor
`mix(white, material_tint, alpha)`, so transparent pixels leave the background
untouched. Both preserve the existing framebuffer alpha.

Special-blend and depth-clipped materials always render in the transparency
stage and do not write to the opaque color, depth, normal, or SSAO buffers,
**even if their original JK material was opaque**. They are still occluded by
the scene depth without corrupting post effects.

### Depth-clipped polygon decals

Set `"depth_clip": true` on a material (or as a pack-wide default) to clip it
against nearby opaque scene geometry. This is independent of `blend_mode` and
`alpha_mode`, defaults to `false`, and is validated by the compiler:

```json
{
  "name": "floor_stain",
  "albedo_map": "stain.png",
  "alpha_mode": "blend",
  "blend_mode": "multiply",
  "depth_clip": true,
  "replaces": ["stain.mat"]
}
```

Place the polygon slightly in front of the receiving geometry. Fragments with
no opaque scene depth, or a depth gap over a **hardcoded 0.15 game units**, are
discarded. Both the opaque scene depth and polygon depth are in view units.
The shader reuses the existing resolved depth texture (also with MSAA) and adds
no new fullscreen passes. Even `blend_mode: normal` depth-clipped materials
are rendered in the transparency stage. For these normal clipped decals,
texture/material alpha is premultiplied to prevent bright fringes.

This is **not** a projector or volume decal: the polygon UVs remain unchanged,
and it does not wrap around curved objects. Transparent-only geometry cannot
receive decals. The fixed gap may need retuning after visual testing.

Their geometry must still be placed against the target surface. Since the renderer
cannot perfectly depth-sort the additive/multiply geometry originally submitted
as opaque triangles against arbitrary transparent world geometry, overlapping
transparent objects may exhibit order-dependent composition. There is no
extra full-screen rendering pass for these effects.

**Test cases:** a partially transparent PNG with a black-ish multiply
tint over a brightly lit floor, alpha-zero regions against several backgrounds,
an additive glow overlapping opaque scenery, and an originally opaque JK face
overridden with a special blend mode. Confirm that `normal` materials and
existing alpha-masked glass remain unaffected, and check with MSAA on/off.
For depth clipping, test a slightly offset stain crossing a floor edge,
an originally opaque JK polygon with `blend_mode: normal`, a floating decal
beyond the fixed gap, empty-background pixels and MSAA silhouettes.
