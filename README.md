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
| `enable_reflections` | Master switch for environment reflections **and directional-sun GGX specularity**; disabling them also skips reflection-only normal/surface map sampling and restores the unreflected diffuse appearance |
| `enable_post` | Enables optional presentation effects (bloom, film grain, vignette, brightness/contrast/saturation); sRGB output remains active when disabled |
| `enable_blur` | Enables distance and peripheral spatial blur (more GPU-intensive) |
| `enable_trails` | Enables lightweight temporal feedback / frame blending |

The existing `enable_ssao` switch remains independent of `enable_post`, making it useful for testing SSAO artifacts. `enable_bloom` independently disables bloom even when `enable_post` is true.

## COG-controlled directional sun and specular highlights

Set a single directional sun per level through COG:

```c
// Direction points TOWARD the sun in WORLD space (JK: X/Y horizontal, Z up).
SetSunInfo('0.4 0.2 1', '1 0.85 0.65', 2.0, 1.5);
vector dir = GetSunDirection();
vector rgb = GetSunColor();
flex intensity = GetSunIntensity();
ResetSun();
```

`SetSunInfo(direction, color, intensity, animTime)` uses independent
wall-clock interpolations for direction, color, and intensity. The
direction is normalized on input and when evaluated; color is clamped
to [0,1], intensity to [0,16]. `animTime` is seconds, with 0 giving an
immediate update. `ResetSun()` disables sunlight immediately, restoring
white light and a direction of `'0 0 1'`. Default intensity is **0**, so
existing levels look unchanged until `SetSunInfo` is called.

Sunlight is world-space, converted to view space from the JK camera's
orientation (camera position is irrelevant). It does not require an
environment map. If the camera orientation cannot be read safely,
the sun contribution is disabled for that frame.

Sun highlights use a GGX/Schlick microfacet approximation and the
material's existing `reflection_strength`, `roughness`, `metalness`,
`surface_map` and normal maps. The new sunlight term is added to
reflections in both the opaque and transparent passes, including fluid
blending, and respects distance fog. It uses the same HDR path as reflections
so bloom can accentuate bright glints. This is SPECULAR ONLY: JK's
vertex-baked diffuse lighting remains unchanged. No shadowing is applied,
so the sun can illuminate surfaces obscured from the sun by other geometry.

These custom verbs require the existing JK.dll `InitNewCogVerbs` hook.
Test rotating the camera without changing the sun position, changing sun
color/intensity over time, normal-mapped water ripples, rough/dull surfaces,
and reflections with no environment image.

### Fog COG changes

Fog no longer has separate enabled state. `SetFogInfo(vector(start, end, opacity), fadeTime)` uses opacity alone, where `0` disables fog. `GetFogInfo()`, `SetFogColor()`, `GetFogColor()`, and `ResetFog()` remain. The old `SetFogEnabled` and `GetFogEnabled` verbs have been removed; existing COGs using those verbs must be updated.

The default fog parameters are `40 1000 0` (off). Setting opacity nonzero fades fog in according to `fadeTime`.


## COG-controlled frame trails

Temporal accumulation is applied to the **finished composited frame** (including
gameplay, HUD, bloom, film grain and color adjustments) in linear color space,
with hardware sRGB output conversion preserved. It needs no changes to
`jk.exe`; the verbs are registered through the existing community patch `JK.dll`
hook.

```c
SetFrameTrail(0.75, 0.5); // Fade to 75% previous accumulated output over 0.5 seconds.
flex amount = GetFrameTrail(); // Current interpolated strength, 0.0..1.0.
SetFrameTrail(1.0, 0.0); // Freeze the last completed frame.
SetFrameTrail(0.0, 0.5); // Fade back to live gameplay.
ResetFrameTrail();       // Disable blending AND discard history immediately.
```

The accumulation formula is `output = current * (1 - strength) + previous * strength`,
where `previous` is the last **blended output**, not the last raw frame.
Strength `0` means no trails; values close to `1` yield a long afterimage;
`1` holds the last displayed frame. Defaults to `0`. A fade always begins
at the presently interpolated value; `fadeTime <= 0` updates immediately.

History is continuously refreshed even at strength 0, so turning on blending
does not show a stale old frame. It is invalidated on menu/game rendering-mode
transitions and when `ResetFrameTrail()` is called. These transitions also reset
the effect's strength to zero to avoid carrying a level effect into the menus.
The engine's per-frame `BeginScene`/`EndScene` hooks do not reset it.

`"enable_trails":false` prevents the history contribution while keeping the
COG-controlled parameters intact; use this for debugging. It does not turn
off bloom or SSAO. Frame blending is also independent of `enable_post`, which
only gates bloom, grain, vignette and color filtering.

**Known limitation:** A direct in-game level-to-level transition that never
enters menu rendering does not currently trigger an automatic reset. Such
transitions should call `ResetFrameTrail()` from a shutdown/startup COG.
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
`enable_trails` controls recursive temporal feedback from `SetFrameTrail`.
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

## Dithered alpha (screen-door transparency)

Material pack metadata now supports `"alpha_mode": "dithered"` in addition to
`"blend"` (default) and `"mask"`. It uses a stable screen-space 4x4 Bayer
pattern, preserving the average transparency as the fraction of surviving pixels.
Coverage includes texture alpha, `albedo_factor` alpha and native 3DO face/vertex
opacity. It does not use `alpha_cutoff` (which remains exclusive to `"mask"`).

```json
{
  "materials": [{
    "name": "foliage",
    "replaces": ["foliage.mat"],
    "alpha_mode": "dithered",
    "albedo_factor": [1, 1, 1, 0.7]
  }]
}
```

Dithered fragments write opaque depth and receive SSAO and distance fog, so
they require no alpha blending or transparency sorting. Both native JK-assisted
world/sector polygons and GPU-resident 3DO meshes use the same 4x4 threshold.
Native JK face translucency (90/255) becomes dither coverage for GPU 3DOs.
Materials using additive, multiply, fluid, depth clipping or emissive-only
blending still follow their dedicated translucent paths.

Dithering is not universally faster: it removes blending/sorting overhead,
but introduces discard and visible screen-door grain; moving the camera can
make the pattern shimmer. It is best suited to foliage, fences and semi-
transparent solid meshes, not glass or fluids.

## Material blend modes

Material packs now support an optional `blend_mode`, independent of the
existing `alpha_mode` (blend/mask). Valid choices:
- `normal` (default): exactly the existing renderer blending behavior
- `additive`: add colored light to the scene, useful for flares/fire/glows
- `multiply`: multiply underlying scene color by a texture-dependent
  factor, useful for blood/dirt/stains
- `fluid`: depth-aware absorption of opaque geometry behind a liquid, with
  reflected environment and emission remaining visible even over shallow
  transparent portions

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


### Fluid blending and absorption

Set `"blend_mode": "fluid"` to treat a textured polygon as a liquid surface.
This reuses the existing resolved opaque linear depth buffer, works with
MSAA, and uses the transparency pass even when replacing an opaque JK MAT.

```json
{
  "name": "lake_water",
  "blend_mode": "fluid",
  "fluid_thickness": 0.08,
  "albedo_map": "lake_color.png",
  "normal_map": "waves.png",
  "normal_map2": "waves.png",
  "normal_strength": 0.25,
  "normal_strength2": 0.15,
  "normal_scroll": [0.012, 0.005],
  "normal_scroll2": [-0.008, 0.015],
  "environment_map": "sky_environment.png",
  "reflection_strength": 1.0,
  "roughness": 0.08,
  "metalness": 0.0,
  "replaces": ["water.mat"]
}
```

`fluid_thickness` is **absorption per game unit**, not the distance at
which water becomes opaque. Higher numbers make water murkier over the same
depth. The default is `0.25`, and 0 disables albedo absorption altogether
(only reflections/emission remain). For example, at an underwater view-path
length of 5 units:

| fluid_thickness | Albedo opacity from absorption |
| --- | --- |
| 0.05 | 22% |
| 0.25 | 71% |
| 1.0 | 99% |

The opacity approximation is `1 - exp(-fluid_thickness * thickness)`.
`thickness` is measured along the eye-to-scene ray, using the depth
difference between the visible liquid polygon and the **first opaque surface
behind it**. Texture/material/vertex alpha continue to cap final coverage,
so use an opaque albedo for fully opaque deep water. If there is no opaque
receiver, the fluid is treated as fully deep. Nearby opaque geometry still
occludes the fluid using the regular hardware depth test.

The alpha-blended albedo fades out in the shallows, while Fresnel environment
reflections and emission retain their independent contributions. This is an
artistic single-pass approximation, not physically complete transmission,
refraction or volumetric scattering. It cannot see through several layers
of transparent geometry, and multiple overlapping fluid polygons are still
subject to the existing transparent draw-order limitations.

`fluid` and `depth_clip: true` cannot be combined: the former regards
empty depth as deep liquid while the latter intentionally discards it.
As with other materials, `fluid_thickness` can be a pack-wide default
overridden per material.

**Testing:** shallow/deep slopes and gaps with no underlying opaque
geometry; grazing angles; strongly reflecting normal-mapped water; opaque
versus alpha-masked albedo; fog on/off; MSAA; foreground occluders; very
high/low `fluid_thickness` and no `fluid_thickness` field. Check that
`normal`, `additive`, and `multiply` blend modes remain unchanged.

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

## Experimental material link diagnostics

A replacement material now retains its original **pack directory**, compiled
material **name**, and whether its environment map was explicitly assigned or
inherited from the pack. A Direct3D texture instance retains a non-owning link
to its resolved replacement definition; the link is reset whenever the instance
is cleared/reused. This does not change material rendering.

With file logging enabled via `"log_path": "jkgm-test.log"` in `jkgm.json`
(or `JKGM_LOG_FILE`), texture uploads
produce messages such as:

```text
Texture instance 83 (signature <md5>) maps to pack <pack path> / material <name>
```

The numeric **texture instance** in these lines is a renderer Direct3D texture
handle, **not** a JK MAT resource index. This logging proves the second half
of the planned JK-MAT-to-pack link but does **not** establish the first half
(JK material index and cel to Direct3D handle). That will require a separate
engine-side investigation. Identical textures may share a replacement definition;
per-instance identity is deliberately not lost. Original jkGfxMod metadata is
accepted unchanged, without mandatory new keys.

## Preserving the original JK albedo

A replacement that only adds surface maps, reflections, or other effects can
preserve the original JK texture. In a **source-pack script** either
`"keep_albedo": true` (existing syntax) or
`"use_original_albedo": true` (alias) works:

```json
{
  "name": "original_with_reflections",
  "use_original_albedo": true,
  "reflection_strength": 0.5,
  "replaces": ["example.mat"]
}
```

Both spellings are also accepted at the source-pack root for inherited defaults
and in precompiled `metadata.json`. The compiler still writes `keep_albedo` in
its output. Never specify both synonyms at the same level, and don't provide an
`albedo_map` alongside a true original-albedo flag.

The original 8-bit JK MAT goes through the game's Direct3D 16-bit upload format
before this renderer sees it, so the original-albedo conversion handles both
original 8- and 16-bit MATs through that same path.

To verify the original upload, set `"log_path": "jkgm-matlink.log"` in
`jkgm.json` and look for
`Keeping original albedo for texture instance ... (D3D source bit depth 16)`.

## Stage 2: JK MAT-index diagnostic

In the experimental `feature/jk-mat-link-diagnostic` build, keep the
patched `jk23.exe` unchanged and enable `"log_path": "jkgm-matlink.log"`
in `jkgm.json`. Load a level with replaced materials (including a multi-cel
MAT), then inspect/upload the log. Every three seconds while in game, the
renderer checks the native JK material tables **without writing to them**.

Look for `JK MAT diagnostic: level=... materials=... validated=...` and
`JK MAT link: id=... name=... cel=... mip=... D3D=... pack=... material=...`.
Only new or changed live replacement links are printed. Original `jk23.exe`
address, function signatures, array stride, and native memory availability
must validate before the scan proceeds. The remaining native texture offsets
are experimental and must be confirmed by the runtime log. No new COG verbs
are registered and no material pack needs recompilation for this diagnostic.

## Reflection fast path and performance checks

The material reflection master switch applies to **both** environment maps and
COG-controlled directional-sun specularity. With
`"enable_reflections": false`, these effects return zero, and normal, environment, and
surface/specular maps are not loaded from files, acquired for binding, or
sampled by the material shaders (for those materials). The same shader fast path also applies to materials without
reflection strength or with neither an environment map nor active sun.

`enable_parallax` is independent: displacement/parallax sampling can still
cost a great deal (the existing parallax shader takes **32–128 heightmap
steps per fragment** for displaced materials). Emissive and fog calculations
are unaffected by `enable_reflections`.

To compare performance, use the same view and resolution in a large level,
and toggle `enable_reflections`, `enable_parallax`, `enable_ssao`,
`enable_bloom`, `enable_blur`, and `enable_trails` one at a time. The
Stage 2 MAT-link diagnostic scans every three seconds only when logging is
enabled; disable `log_path` for performance benchmarking. Scene complexity,
triangle sorting and uploads, transparency overdraw, and state-change
frequency remain possible CPU/GPU bottlenecks that require measurements.

### Installing the reflection fast-path test build

The Windows CI artifact `jkgrfxmod-reflection-fastpath-test` contains
`renderer.dll` **and** two updated fragment shaders:
`jkgm/shaders/game_opaque_pass.frag` and
`jkgm/shaders/game_trns_pass.frag`.
Install them in the corresponding game directories after backing up the
existing files. A DLL-only swap does **not** activate the shader fast path.
Keep using your patched JK executable; this PR does not change it.

## Experimental runtime MAT material controls

Material packs may now declare optional **pack-local** numeric image IDs in their
source script. These images are copied and converted with the regular sRGB
premultiplied-alpha pipeline even when they are not referenced by any material.
Old compiled packs with no `images` field remain valid.

```json
{ "name":"Example", "description":"COG-controlled material images",
  "images":[{"id":1,"file":"sky.png"},{"id":2,"file":"paint_blue.png"}],
  "materials":[{"name":"paint","replaces":["carpaint.mat"],"reflection_strength":1.0}] }
```

Recompile the pack with the `compile.exe` from this experimental build. Its
compiled `metadata.json` includes `images` and the packaged image files.
IDs range from 0 to 65535 and are unique within each material pack.

| COG verb | Effect |
| --- | --- |
| `SetGlobalEnvMap(sourceMat, imageID, fadeTime)` | EnvMap from source MAT's pack on all replacement materials without an explicit EnvMap, across packs |
| `ResetGlobalEnvMap(fadeTime)` | Return to pack defaults |
| `SetMatEnvMap(mat, imageID, fadeTime)` | Per-native-MAT EnvMap override |
| `SetMatEnvMapFrom(mat, sourceMat, imageID, fadeTime)` | Per-MAT EnvMap from another MAT's pack |
| `ResetMatEnvMap(mat, fadeTime)` | Return to global or pack default |
| `SetMaterialAlbedo(mat, imageID, fadeTime)` | Change all cels' albedo using the MAT's own pack IDs |
| `ResetMaterialAlbedo(mat, fadeTime)` | Restore original/replacement albedo |
| `SetMaterialColor(mat, rgb, fadeTime)` | Multiply base albedo RGB by a vector with components from 0 to 4 |
| `ResetMaterialColor(mat, fadeTime)` | Fade the RGB multiplier back to (1,1,1) |

```c
// Example COG calls, using a material symbol `carpaint`:
SetMatEnvMap(carpaint, 1, 1.5);
SetMaterialAlbedo(carpaint, 2, 0.75);
SetMaterialColor(carpaint, '0.8 1.0 1.0', 0.75);
ResetMaterialAlbedo(carpaint, 1.0);
```

MATs and all cels are resolved through the validated native JK material
tables. Source MATs need at least one mapped replacement texture to identify
their originating pack. A command remains pending if the relevant texture
has not yet been uploaded. World changes clear commands to avoid handle reuse.
An image fade samples two 2D images; interrupting a fade currently starts
the new transition from the prior target instead of a three-way composite.
EnvMaps in this renderer are equirectangular 2D images, not cubemap arrays.
`enable_reflections=false` continues to disable runtime reflections.

### Testing installation

The CI artifact `jkgrfxmod-material-runtime-test` includes `renderer.dll`,
`compile.exe`, and both updated GLSL shaders under `jkgm/shaders`.
Install the two shader files along with the DLL; otherwise the new image
transition uniforms are unavailable. The test remains experimental until
the shader paths and COG semantics are verified in game.

### Runtime material refresh/regression diagnostics

The MAT resolver now refreshes immediately when any COG material command
changes, or after returning from the game menu, instead of waiting up to
one second. The renderer verifies that a cached native MAT association
belongs to the same D3D texture object **and upload generation**, protecting
against recycled Direct3D IDs.

While debugging, enable `log_path` and look for:
- `COG SetMaterialAlbedo: MAT=... image=... fade=...`, or the matching reset
- `Runtime MAT resolver: revision=... cached handles=... pending=...`
- `Runtime image ... native source MAT ... has no replacement pack mapping yet`
- `Unregistered image ID ... in source MAT ... pack ...`
- `Runtime EnvMap requested but enable_reflections=false`

Each material image ID is relative to the *recompiled source pack containing
the supplied MAT*. A vanilla JK MAT can receive `SetMaterialColor`, but
`SetMaterialAlbedo` and `SetMatEnvMap` need a matching **replacement
material/pack** from which to resolve the image ID.

For a reliable EnvMap test, use `enable_reflections: true`, a material
with `reflection_strength: 1.0` (or higher for obvious test reflections),
low roughness, and an equirectangular image with strong light/dark contrast.
`SetGlobalEnvMap` deliberately skips materials with an explicit
per-material environment image. Passing a nonzero fade time is mandatory
for the material and reset COG verbs, even if it is `0.0`.

Repeated set/reset sequences are exercised by the lightweight native
`tests/runtime_material_state.cpp` regression test in Windows CI.

## Optional CPU/geometry frame profiler

Set `"profile_renderer": true` in `jkgm.json` and configure `log_path`.
It emits four `PERF` log lines about every two seconds, reporting:

- **frame_period_us**: CPU wall-clock interval between consecutive game
  presentations (includes JK simulation, synchronization and other uninstrumented work)
- **submit_us**: wall-clock time inside the mod's `present_game`
- **execute_us**: summed CPU time decoding all Direct3D execute buffers
- **sorting_us**: independent opaque/material and transparent/spatial sorting;
  the log also records maximum sort time in the window
- **upload_us**: preparing and uploading geometry vertex buffers
- **passes_us**: the opaque/SSAO/compositing render pass and transparency pass
- **post_swap**: postprocessing and `SwapBuffers` (may include GPU synchronization)
- **triangles**, **transparent_avg**, **draw_batches_avg**, **material_binds_avg**:
  submitted triangles, GL triangle draw calls and renderer material binding calls
- **JK-side gaps_us before_first / between / after_last**: CPU wall time
  outside the replacement renderer, divided by all frames. `before_first`
  spans the end of the previous present to the first Direct3D execute-buffer
  call. `between` sums the gaps between execute-buffer calls; `after_last`
  spans the last execute-buffer return through the start of game presentation.
  These are **not pure native 3DO timings**: they can include gameplay, COG,
  driver waits, or other engine operations. A frame with no execute buffers
  records no gap samples, hence the accompanying `frames_with_execute` count.
- **execute_buffers**: mean Direct3D execute-buffer calls, submitted vertex
  entries, and execute-instruction bytes per frame. A vertex may be referenced
  multiple times or resubmitted in several buffers; these aren't distinct
  model vertices.

When profiling is enabled, exact-signature hooks on this project's patched
JK23 executable also report native_3do inclusive_us model/mesh plus call counts.
Model timings INCLUDE nested mesh timings, and mesh timings include faces,
lighting and clipping: do not add the timings together. The optional
face timer covers per-face clipping, lighting, projection, and cache
submission, but NOT the parent mesh's vertex transformations or
backface tests. `model_drawn_avg` counts non-culled model calls,
`face_calls_avg` counts faces reaching rdModel3_DrawFace, and
`face_emitted_avg` counts successful face processing after clipping.
Per-face clock reads add some profiling overhead; compare the timings
from separate profiling versions before using absolute FPS as a benchmark. These are only 3DO model
calls; static level geometry is a separate engine path. An unrecognized EXE
simply skips native hooks and logs a warning. No native hooks are installed
when profile_renderer is false.

All times are **microseconds per frame, CPU-side**, unless explicitly marked peak.
For example, 10000 us = 10 ms. These timers do **not** measure GPU execution
time and their stage totals need not sum to the full frame period. The post/swap
and draw pass timings can include driver waits. When profiling is enabled, the
old periodic native MAT diagnostic scan is suppressed to reduce noise.

For comparative measurements: set `enable_vsync: false`; compare the exact
same camera position and resolution in (1) a small enclosed scene, (2) a large
open scene, (3) the same large scene with reflections/parallax/SSAO disabled.
Then repeat at half render resolution. Keep both shader versions and game
position identical. Compare the averages and peak values, not a single FPS
sample. Setting `profile_renderer: false` returns the normal low-overhead
path. If `log_path` is unset, the measurements will not be persisted to disk.

## Faster JK vertex-color decoding (default)

The original execute-buffer decoder calculated nine 1.8-gamma power functions
for each submitted triangle: three RGB channels per vertex across three
vertices. For JK's 8-bit vertex colors, each premultiplied RGB channel has
only 256 possible values and each vertex alpha has 256 possible values.

The renderer now **always** uses a shared 256 × 256 float lookup table
(256 KiB) indexed by input color channel and alpha, preserving the
premultiply-before-gamma order. It also preserves per-vertex RGB tinting.

This replaces the tested `fast_vertex_colors` experiment. The JSON setting
is no longer needed; it is ignored if present in an older configuration.
The lookup table is initialized on first use. The C++ regression test
compares table values with the previous floating-point calculation.

The high-triangle stress test (~21,200 triangles) reduced
`execute_us` from approximately 18.4 ms to 3.0 ms, with no
noticeable lighting differences reported in game. Because the original
engine's 3DO preparation remains outside the decoder, overall FPS improved
less than the decoder itself. The next optimization stage targets JK's
native model traversal and geometry preparation.


## Direct executable startup (development)

JkGRfxMod no longer builds or ships an injection launcher. The standalone
`jkgm.exe` is created by patching the **known compatible** 32-bit
`jk23.exe` with `tools/patch_jkgm_bootstrap.py`:

```sh
python3 tools/patch_jkgm_bootstrap.py /path/to/jk23.exe /path/to/jkgm.exe
```

The patcher checks the full SHA-256 of the user's original EXE
(`1610d070112b7f3a78668ede5deab9aeebc758e83ec4c69aa705c2e91f64a0da`);
unknown executables are rejected, and no game executable is included in CI or
release archives. A preexisting `jkgm.exe` from this method does not need to be
repatched unless you are regenerating it from the original input.

The loader adds a new PE section that calls the game's existing
`LoadLibraryA` import on `renderer.dll` before continuing to the original
entry point. The DLL and shader/material assets remain separate. It is
"standalone" only with respect to the old launcher: you still need
`renderer.dll`, `jkgm.json` and `jkgm/` alongside the game.

Start `jkgm.exe` directly and pass any desired JK switches yourself (for
example `-path gunrunner`). The legacy launcher previously managed some
options, including `-windowgui`, but the new executable does not do that.
The obsolete `command` JSON option has been removed.

Set `"use_vanilla_renderer": true` in `jkgm.json` to load the DLL without
installing the renderer or COG hooks. With `false` (default), JkGRfxMod hooks
are installed at process startup. The choice requires restarting the game.
Without `renderer.dll`, the bootstrap still runs the original JK startup,
which means it may use the native renderer instead.

The existing mesh-limit, RGB vertex tint and normal sideband patches are
preserved. Native engine geometry profiling and culling remain separate work.

For exact install paths, compatibility limits, and test commands, see
[doc/install.md](doc/install.md).


## Retro Mode: first-CMP 256-color presentation

Set `"retro_mode": true` in `jkgm.json` (default `false`) to quantize
the **final displayed frame**, including HUD, bloom and accumulated trails,
to the 256 RGB entries of the FIRST loaded CMP (index 0), never the active
sector CMP. No dithering is performed: the original software-rendered
color-band look is intentional. The normal hardware geometry path remains
enabled; JK's software-mode indexed lighting tables and resolution limits
are not simulated.

The palette is read from the JK23 identity CMP in game memory when entering
a level, and a 64x64x64 nearest-RGB lookup is generated only if its contents
change. CPU kd-tree construction makes lookup generation efficient, while
the final presentation shader performs just one atlas fetch per pixel.
Menus do not use this effect. If the known JK23 identity CMP pointer is
unavailable the renderer automatically falls back to full-color output.
This works with either the native or GPU-resident 3DO renderer and does not
depend on `enable_post`.

**Exact CMP verification:** Enable `"retro_mode": true` and
`"log_path": "jkgm-retro.log"`, then enter a level. The `Retro CMP verify:`
lines identify the engine identity-CMP pointer and the current world's
`colormaps[0]` pointer, compare their RGB bytes and print stable checksums.
Sixteen `Retro CMP #0 RGB[N]:...` lines dump the actual 768 RGB8 bytes
read by Retro Mode, emitted only on palette/world changes.

Extract the FIRST CMP in the JKL's `World Colormaps` section, normally
from `misc/cmp` in the level's GOB, and compare:

```sh
python tools/verify_retro_cmp.py misc/cmp/first.cmp jkgm-retro.log
```

That script reports exact mismatches by palette index. It correctly
reads the on-disk CMP's RGB table at offset `0x40` (the loaded engine
structure keeps those same 768 bytes at offset `0x30` due to its
32-byte debug-name prefix). A matching
palette proves the acquisition step and points to a downstream problem
such as quantization or color processing. For a clean visual test,
temporarily set `"enable_post": false` to disable color grading.

## GPU 3DO point-light specular (currently suspended)

Native JK dynamic point lights currently affect only the original
diffuse/Gouraud illumination of GPU 3DOs. Their additional GGX
point-light specular term is deliberately disabled until there are
separate dynamic-light RGB and specular-intensity controls. The shader
implementation remains available for future work. Environment maps
and COG-controlled directional sun specular are **unaffected** and
continue to follow `enable_reflections`.
GPU 3DOs now also support static/per-material and pack-default EnvMaps
(equirectangular world/screen/fixed/matcap modes) plus a second normal-map
layer, with normal scroll and mip-based roughness blur. These use the native
material properties. Directional-sun GGX specular now also follows `SetSunInfo` on GPU 3DOs,
using the same shader model as native material rendering. The sections
below document the additional COG image-transition, displacement,
fluid, and depth-clipping parity work. This is not SSR, shadow mapping,
a true projected decal system, or an area-light approximation.
Benchmark scenes with strong reflections and many nearby lights: these
are per-fragment GGX loops, which can be expensive on large polygons.

### COG runtime material parity on GPU 3DOs

GPU-resident 3DO materials now follow the same stable MAT-ID mapping used
by JK-assisted draws, including D3D-handle generation validation. Runtime
`SetMaterialColor` adjustments, per-MAT albedo crossfades, global EnvMap
transitions and per-MAT EnvMap overrides resolve against the same registered
replacement-pack images. EnvMap commands may enable reflection on an
otherwise matte replacement just as in the native path. The old geometry
cache is not rebuilt when these shader-only material values change.

### GPU-resident parallax material parity

GPU 3DO replacements with a `displacement_map` and nonzero
`displacement_factor` now perform the same opt-in parallax-occlusion
UV offset used by native materials. The tangent frame uses original
mesh-local positions and normal/UV derivatives. The adjusted coordinates
feed the albedo, emission, normal, and reflection samplers. With
`enable_parallax:false` or no displacement map, there are no height
samples. Very large displacement factors may require further tuning.

### GPU-resident fluid and depth-clipped polygon decals

GPU 3DO materials now use the same opaque-depth-based Beer-Lambert fluid
absorption and the same 0.15-JK-unit perpendicular-plane-gap rejection
for depth-clipped decals as the native material shader. Both run only in
the GPU transparency pass and sample the resolved R16F opaque depth.
Thin water loses pigment while preserving reflection/emission.
This is not volumetric projector decal rendering.

## Native JK hardware color effects and sector-screen tint

JK hardware acceleration copies the accumulated stdPalEffects via
`rdSetColorEffects` (0x0043AE60) into `rdroid_curColorEffects` (0x00889E00).
It modifies RGB vertex colors **before** material texturing, not the final
screen image. Native D3D triangles already carry the color correction; applying
it again to the final composite would double-tint native geometry and tint
the UI incorrectly.

GPU-bypassed 3DO vertices now read and approximate the *hardware* color-effect
state once per scene pass. This supports sector-screen tint (`SetSectorTint`),
`NewColorEffect`, `AddDynamicTint`, `AddDynamicAdd`, filter and fade,
without modifying JK.dll. Exact 8-bit rounding still requires comparison.
Software mode instead modifies 8-bit palette entries and remains unsupported.
GPU 3DOs now support `SetSectorColormap`: the native rdColormap
current/identity pointers select the CMP, and its RGB header tint is
sampled per mesh instance. This follows the native hardware vertex-color
path, with no mesh re-upload when a COG changes colormaps. Software CMP
light tables are intentionally not used.

The reader verifies the 32-bit JK23 instruction signature and defaults to
neutral coloring if unsupported. Dynamic JK.dll model mutations remain to be
tracked with targeted GPU mesh-cache invalidation.

## Experimental GPU-resident 3DO mesh path

Set \`"use_model_cache": true\` in \`jkgm.json\` to draw **all supported
filled 3DO meshes** from cached GPU vertex/index buffers. Set \`false\`
(default) to use the previous JK-assisted 3DO renderer, while retaining
JkGRfxMod's normal OpenGL world/sector rendering. This is not
\`use_vanilla_renderer\`, which disables JkGRfxMod entirely.

The GPU renderer snapshots 3DO face-corner positions, UVs and face normals
once and updates only world/joint matrices per frame. Simple concave face
outlines now use projected ear clipping instead of the incorrect triangle
fan. Convex faces preserve their original fan diagonals and UV interpolation.
Mildly warped/nonplanar polygons keep JK's per-face backface criterion rather
than losing triangles through OpenGL culling. Self-intersecting or degenerate
outlines fall back for the **whole mesh** to native JK.
Texture surfaces are warmed using a fallback native draw when needed.
Unsupported 3DO geometry (such as wireframe/point faces or invalid/missing
data) also falls back to native JK. Cached meshes now compute a
guarded byte signature of their position, UV, face, normal, static-light and
vertex-color buffers once per distinct mesh per frame; edits by JK.dll verbs
invalidate that mesh even when all pointers and counts remain unchanged.
Snapshot failures are memoized until these source bytes change, avoiding
repeated failed triangulation of uncacheable meshes. Changes made between two
draws of the *same mesh in the same frame* remain a known narrow limitation.
This source check has measurable CPU overhead: enable `profile_renderer`
and inspect `PERF gpu_3do source_check_us_avg`, `checks_avg` and
`invalidations_total` before judging the benefit of persistent caching.
Legitimately untextured faces use a neutral color for now;
their exact JK flat colors still require lighting/material parity.
The engine still renders world-sector geometry through the original path.
The GPU path now reconstructs JK's ambient floor, per-face extra light,
per-vertex static intensity, diffuse/Gouraud lighting and ordinary dynamic
point lights in its vertex shader. Ambient and light positions are captured
per Thing at mesh-interception time, so cached 3DO geometry stays resident
even as lighting and COG light-mode overrides change. The shader applies
JK's gamma-1.8 vertex-light conversion. Up to 16 nearby point lights are
supported; unsupported lighting state or additional nearby lights retain
the native fallback rather than silently omitting illumination.

Multiply and Additive materials use their own GPU blend outputs: Multiply's
transparent pixels produce a neutral white factor, while Additive preserves
the target framebuffer alpha. These are independent of standard blend/mask
alpha routing.

The GPU vertex path snapshots the optional 3DO RGB vertex tints from the
patched executable's extra per-mesh color array once per mesh (falling back
to white when absent) and multiplies them with native-style Gouraud/diffuse
lighting in the vertex and fragment shaders. Exotic dynamic light types
(including spotlights), normal maps and reflection effects remain unsupported.
GPU fog and SSAO use the native 32x linear view-depth scale. GPU depth
is now reconstructed per fragment from reciprocal clip W, avoiding clamped
vertex-depth interpolation on large faces crossing the camera plane.
Opaque fog is applied in the composite after SSAO, so fog itself is not
darkened by occlusion. The GPU path uses CPU backface rejection for wholly
back-facing material groups, in addition to ordinary OpenGL face culling.

Alpha MATs with mixed opacity use disjoint fragments in two passes: fully
opaque texels enter the opaque depth-writing G-buffer and SSAO; partially
transparent texels are blended back-to-front. Alpha masks use the configured
cutoff. Faces marked 0x1 are two-sided; 0x2 forces translucency. Transparent
GPU face batches sort against other GPU 3DOs, but not yet against all native
sector polygons.

Emissive-only replacements with albedo factor \`0 0 0 0\` preserve the
zero-alpha base and composite their glow directly in the transparency pass,
without writing opaque depth or turning their base solid black. Other
emissive materials participate in the normal opaque G-buffer/composition.

Opaque GPU draw ranges are now sorted per instance by material/cel/face flags
and dispatched through glMultiDrawElements when compatible, reducing tens of
thousands of per-face GL submissions. The transparency pass remains sorted
per face for blend correctness. PERF gpu_3do batch reports the actual GL call
count, source index ranges, and multi-draw calls; draws_avg now means GL
submissions rather than original source ranges. This is an initial benchmark
for driver submission overhead, not final static index-buffer compaction.

The GPU path additionally rejects entire material-group draw ranges using
conservative transformed AABBs against the native frustum. This can save
GPU submissions for large visible meshes whose material groups are offscreen;
it does NOT yet match native per-face frustum clipping inside a partially
visible group. New PERF gpu_3do group counters expose frustum rejects and
backface rejects (both counted across opaque and alpha passes).

The GPU path now preserves JK's own per-mesh radius/sphere-frustum rejection;
the initial all-mesh bypass omitted it and drew offscreen geometry. This is
the existing sphere test, not the discarded mesh-AABB system. JK also filters
Thing Flags (including 0x10, not rendered) before calling our mesh hook.

Replacement RGBA albedo alpha is now inspected once at upload, rather than
guessing from JK's original MAT alpha flag. Fully opaque textures are
single-pass; textures with transparent pixels use the per-pixel mixed-alpha
split. GPU MAT resolution is cached once per unique material/cel per frame
and native residency is tested once per mesh per frame, even for multiple
instances. With `profile_renderer` enabled, the log also reports the first
80 distinct mesh-name/fallback-reason combinations, and separates opaque and
transparent GPU GL submissions. These identify meshes which intentionally
continue through JK, and whether alpha sorting prevents further batching.

The development CI workflow is manually triggered with **Actions → CI →
Run workflow** on the `development` branch, not automatically on each push.
The test artifact includes `post_opaque_composite.frag` along with the updated
GPU and opaque-pass shaders. Always install the supplied shaders with the DLL.

For performance diagnosis, compare `"use_model_cache": true` and `false`
with `"profile_renderer": true`, `"enable_vsync": false`, and a `log_path`.
The new `PERF gpu_3do` lines report candidate meshes, sphere rejections,
instances, MAT residency checks, draw calls, triangle counts, texture
resolutions and CPU-side GPU submission cost. These are CPU timings, *not*
actual GPU execution time. Disable profiling for final FPS measurements.
GPU instancing and cross-mesh material batching remain unfinished.

The new path requires the updated \`renderer.dll\`, plus
\`gpu_3do_preview.vert\` and \`gpu_3do_preview.frag\` from the same
build in \`jkgm/shaders/\`. Keep the native \`jkgm.exe\` bootstrap.


## Native MAT mip selection

The original Sith renderer chooses a particular mip level from a MAT before
uploading its DirectDraw texture. If an object is first encountered from far
away, that may leave JkGRfxMod with only a reduced-resolution source image.
OpenGL cannot reconstruct the original detail by generating additional mips.

For the supported, verified JK23/jkgm executable builds, JkGRfxMod now
intercepts the native mip-distance setter and forces JK to request **native
MAT mip 0** (full original resolution) at ordinary viewing distances. This
applies to both native sector surfaces and GPU-rendered 3DOs; level/video
mode updates cannot silently restore JK's low-resolution selection.
MATs without a level-0 image still use the engine's nearest-available-mip
fallback.

**OpenGL mipmapping is not disabled.** JkGRfxMod generates GPU mipmaps
from the highest-resolution source through its existing texture cache and
selects them dynamically using normal filtering/LOD, including anisotropy.
No additional config option or EXE patch is needed. Unsupported executables
keep their original native mip behavior. The override does not run with
`"use_vanilla_renderer": true`.

## Experimental extended SPR type foundation (JK23 only)

The checked-in `tools/patch_jkgm_sprite_types.py` verifies the exact
development JK23 executable SHA-256 and changes *only* the `sithSprite_Load`
maximum type from 2 to 63. It refuses unknown EXEs and never overwrites the
input. Example (run on a copy of your executable):

```sh
python tools/patch_jkgm_sprite_types.py jkgm.exe jkgm-spr.exe
```

The experimental SPR draw hook now honors the additional orientation bits:
`0x04` preserves the Thing Up vector, `0x08` its Look vector,
`0x10` its Right vector; combining two preserves the complete plane.
`0x20` reserves decal projection and *currently only produces an oriented
flat quad*, **not yet a depth-projected decal**. For testing an upright
billboard, set the SPR's decimal type to `4` and use an unmodified sprite
material. For a fixed-orientation flat sprite, use type `12`
(`0x04 | 0x08`). Types 0, 1, and 2 stay on the original unmodified
native renderer path. The hook modifies only the four camera-space vertex
positions before JK's original primitive clipping; MAT sampling, UVs and
lighting stay native. It uses signature checks for both hook addresses
and stays disabled with `use_vanilla_renderer=true`.

The SPR loader patch is necessary before types higher than 2 can load.
`SetSpriteInfo` (per-Thing scaling/opacity/fading), true projected decals,
and textured particle rendering remain to be implemented and tested in-game.
The patch script ships with CI test builds; the repository distributes no EXE.

## Experimental depth-projected decal (first prototype)

A projected decal is **not** a MAT material alpha mode. The renderer uses the
shared opaque G-buffer's linear depth, normals and the active JK render camera
to reconstruct the receiving surface's world position. It projects an RGBA
image through an oriented rectangular volume, rejecting geometry outside
its width/height/depth or facing away from the projector. This makes the same
decal work on **native and cached GPU 3DOs, as well as level sectors**, without
editing any existing polygons.

Add an optional **single** projector to `jkgm.json`:

```json
"projected_decal": {
    "image": "jkgm/decals/test.png",
    "position": [0, 0, 2],
    "direction": [0, 0, -1],
    "up": [0, 1, 0],
    "size": [4, 4, 2],
    "opacity": 0.8,
    "normal_threshold": 0.25,
    "blend": "multiply"
}
```

Here `position` is the projector's **world-space origin**, `direction`
points **into** the receiving surface, and `up` rotates the image around
the projection axis. `size` is `[width, height, projection_depth]` in
native JK world units. The image is centered on the projector's origin,
with the depth extending forward along its direction. Position and image
are intentionally test values: change them for your level. The referenced
PNG must exist; no example image is shipped. Remove `projected_decal` or
set it to `null` to disable projection completely.

The optional `blend` can be `multiply` (default; dark tire marks and dirt),
`alpha` (colored decals), or `additive` (a first step toward projected lights).
It is **specific to the projector** and does not change MAT blend behavior.
`opacity` multiplies the PNG alpha; `normal_threshold` requires a receiving
normal to face back toward the projector (0 is permissive; 1 restricts it to
fully facing surfaces). Projector bounds fade slightly to prevent hard depth
cutoffs. Sampling occurs after SSAO, before distance fog and under existing
specular/emissive highlights. Retro 256-color quantization applies normally.

**Prototype limitations:** This first version is static and holds exactly one
projector in `jkgm.json`. It affects **opaque surfaces** only; transparent
water/glass, and moving Thing-attached projectors need a separate extension.
Opaque viewmodel/gun geometry currently shares the G-buffer, so it is not yet
explicitly excluded from receiving a projector. HUD/menu elements drawn later
are unaffected. Alpha/color decals currently do not automatically
inherit the receiver's per-vertex lighting intensity; `multiply` respects the
underlying shading best for dark marks. No runtime COG verbs or pooling/expiry
for tire marks yet. The compositing shader skips all projection work when
the setting is absent.

