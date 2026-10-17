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


## Normal maps and two-layer water ripples

Normal mapping is supported in both opaque and transparent materials. Maps
must be tangent-space **OpenGL +Y** normal textures (RGB, neutral
approximately `[128,128,255]`). They are imported and sampled as **linear
data**, without sRGB conversion or alpha premultiplication. Only the surface
reflection direction changes; JK vertex lighting and SSAO continue to use
geometry normals. A material must have an `environment_map` (or inherit the
pack's default environment map), plus nonzero `reflection_strength`, for
the normal perturbations to produce a visible reflection.

Two layers are blended by adding their tangent-space slopes and converting the
result into a new per-pixel view normal. Both layers can scroll independently
using the renderer clock without animating the albedo or emissive textures:

```json
{
  "name": "lake_water",
  "albedo_map": "lake_color.png",
  "environment_map": "sky_environment.png",
  "normal_map": "waves.png",
  "normal_map2": "detail_waves.png",
  "normal_strength": 0.35,
  "normal_strength2": 0.15,
  "normal_scroll": [0.012, 0.005],
  "normal_scroll2": [-0.008, 0.015],
  "reflection_strength": 1.0,
  "roughness": 0.08,
  "metalness": 0.0,
  "alpha_mode": "blend",
  "replaces": ["water.mat"]
}
```

- `normal_map` enables the first layer; `normal_map2` enables a second
  layer and requires `normal_map`. Using the same file for both layers works.
- `normal_strength` and `normal_strength2` are slope multipliers, default
  1.0, valid range 0–4; values 0 disable that layer's effect.
- `normal_scroll` and `normal_scroll2` are UV repeats per second,
  default `[0, 0]`, independent of `albedo_scroll` and `emissive_scroll`.
- The normal maps' UVs are still derived from the model's own base UV layout,
  but the *scroll offsets* are independent. `albedo_scroll` moves **only**
  the albedo sampler, not surface, displacement, emissive, or normal maps.
- Normal layers do not add new framebuffers or render passes; they cost up to
  two more texture samples and one per-fragment tangent-basis computation.
- They do not automatically make flat water transparent, refractive, or
  depth-absorbing; those are separate water shading features.

For water, start with a stationary water-color albedo and two differently
scrolling normal layers. Confirm diffuse color remains stationary, the
reflection ripples move even with `albedo_scroll` omitted, and no effect
occurs with either normal strength at zero. Test on tilted/mirrored-UV meshes,
with MSAA, and on opaque and transparent surfaces.

## Animated materials

Animations use the renderer's own monotonic clock, not JK's simulation clock.
They continue advancing while gameplay is paused, provided frames are drawn.
No video frames or texture reuploads are needed: the renderer applies per-material
UV offsets and an emissive multiplier at material-bind time. Both opaque and
transparent render paths support them.

All properties are optional, can be provided as pack-wide defaults, and can
be overridden per material:

```json
{
  "name": "energy_strip",
  "albedo_map": "base.png",
  "emissive_map": "glow.png",
  "emissive_factor": [1.0, 0.5, 0.2],
  "albedo_scroll": [0.0, 0.05],
  "emissive_scroll": [0.0, -0.1],
  "emissive_pulse": {
    "wave": "random",
    "rate": 2.0,
    "min": 0.2,
    "max": 1.0,
    "phase": 0.0
  },
  "replaces": ["strip.mat"]
}
```

- `albedo_scroll` and `emissive_scroll`: UV repeats per second in U and V.
  Texture coordinates wrap by repetition, and positive/negative values scroll
  in opposite directions. Albedo and emissive scroll independently; the
  displacement, reflection/surface, and environment maps are not scrolled.
- `emissive_pulse` multiplies the complete `emissive_factor` vector, and works
  even without an emissive map. `rate` is in seconds; `min` defaults to 0,
  `max` to 1, and optional `phase` is a fraction of a cycle (default 0).
  Multipliers above 1 are allowed for intense emissive flashes.
- `sine`: one smooth full cycle per `rate` seconds.
- `square`: alternating high/low, with a full on/off cycle lasting `rate` seconds.
- `flicker`: one randomly selected on/off state every `rate` seconds.
- `random`: smoothly interpolates between new pseudorandom targets every
  `rate` seconds, without visible jumps at interval boundaries.

Randomness is deterministic per material instance and interval, not per frame,
so the pattern is independent of framerate and draw order. Animations do not
modify emissive/albedo opacity handling, blend modes or depth clipping.

**Animation test cases:** scroll each layer separately and together, test
negative U/V speeds, ensure UVs wrap, verify `sine`/`square` timing and
`flicker`/`random` stability at different FPS, check blinking with
`emissive_factor` only (no `emissive_map`), and test both opaque and
depth-clipped surfaces.

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

Place the polygon slightly in front of the receiving geometry. Fragments
without opaque scene depth, or with more than **0.15 game units of perpendicular
separation from the decal's polygon plane**, are discarded. Unlike comparing raw
view-depth differences, this remains stable at oblique/grazing camera angles.
The shader reconstructs view-space positions using the existing resolved depth
texture (also with MSAA), derives the decal's geometric normal with screen-space
derivatives, and adds no new fullscreen passes. Even `blend_mode: normal`
depth-clipped materials are rendered in the transparency stage. For these
normal clipped decals, texture/material alpha is premultiplied to prevent
bright fringes.

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
beyond the fixed gap, empty-background pixels, grazing-angle views,
and MSAA silhouettes. Also test decals with smoothed vertex normals.
