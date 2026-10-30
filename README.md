# JkGRFxMod

A modification of JkGfxMod specifically built for JkGR


## Opt-in accurate COG raycasting: RayCastEx

RayCastEx is a **COG-only** collision query. Native JK type-3 raycasts,
projectiles, LOS, and physics are unchanged: the former experimental global
precise collide-3 hook is disabled. No JSON toggle is necessary.

```c
ray = RayCastEx(sourceThing, maxDistance);
if (ray != -1)
{
    position = GetRayHitPos(ray);
    normal = GetRayHitNormal(ray);
    distance = GetRayHitDistance(ray);
    thing = GetRayHitThing(ray);          // -1 for world-surface hits
    surface = GetRayHitSurface(ray);      // -1 for model-Thing hits
}
```

The ray begins at the source Thing's position and travels along its **Look**
vector for `maxDistance` world units. It returns the closest intersection,
or -1 on miss or invalid arguments. Returned handles rotate through five
slots per COG, shared with `RayCast`; consume the result before issuing many
additional rays. **No temporary Ghost Thing is spawned.** To place an actual
Thing at the hit location, use COG's normal Thing creation methods with
`GetRayHitPos(ray)`.

This independent ray path tests blocking static world polygons in double
precision using the existing world-geometry BVH, and tests collide=2/3
model Things with the preserved animated-node precise polygon solver. Sphere
colliders use an analytic sphere intersection. It is deliberately not involved
in ordinary gameplay collision and should be used sparingly. The initial
implementation scans live Thing slots, using an expanded Thing collision
sphere as a candidate filter; very badly off-center 3DOs with undersized
`collideSize` can still be excluded. Portal/adjoin rules follow the
static world receiver snapshot, not the engine's exact sector traversal,
so compare hit semantics against your target levels during testing.

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
| `enable_post` | Enables optional presentation effects (bloom, HDR, film grain, vignette, brightness/contrast/saturation); sRGB output remains active when disabled |
| `enable_blur` | Enables distance and peripheral spatial blur (more GPU-intensive) |
| `enable_trails` | Enables lightweight temporal feedback / frame blending |

The existing `enable_ssao` switch remains independent of `enable_post`, making it useful for testing SSAO artifacts. `enable_bloom` independently disables bloom even when `enable_post` is true.

## Default material reflectivity

Materials with no explicitly authored reflection scalars and no RGB surface map
are matte by default:

```json
{"reflection_strength": 0.0, "roughness": 0.0, "metalness": 0.0}
```

Materials with an authored `surface_map` but no scalar overrides instead use
`1.0, 1.0, 1.0` multipliers, so the map's R/G/B channels determine the
per-pixel reflectivity, roughness and metalness without weakening their values.
Explicit per-material scalar settings always take priority. With no reflection
strength, environment and directional-sun GGX highlights remain disabled; no
fallback environment texture is created. Explicit COG environment-map changes
can still activate otherwise matte materials, and `enable_reflections: false`
disables reflection rendering globally.

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

### Additional COG math verbs (JK23)

JkGRfxMod registers five pure numeric verbs through JK.dll's
`InitNewCogVerbs` entry point. They work with COG numeric arguments and
do not depend on the renderer being enabled:

| Verb | Returns | Meaning |
| --- | --- | --- |
| `Min(a, b)` | flex | The smaller of two numbers |
| `Max(a, b)` | flex | The larger of two numbers |
| `Clamp(value, low, high)` | flex | Limit a value to the inclusive range; reversed bounds are swapped |
| `Sign(value)` | int | `-1` for negative, `0` for zero, `1` for positive |
| `Lerp(a, b, t)` | flex | `a + (b - a) * t`, **unclamped** |

For example, `Clamp(15, 0, 10)` returns `10`, and
`Lerp(10, 20, 1.5)` returns `25` (extrapolation). `Lerp` does not
restrict `t` to the range 0–1; use `Clamp` explicitly if desired.
JK23 already supplies `Absolute`, `Floor`, `Ceiling`,
`SquareRoot`, `Power` and the `%` operator; they are not reimplemented.

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


## Default vignette behavior

The base game has **no vignette**. JKGM now starts with strength 0, and
`ResetVignette()` also returns to 0. A level or mod may explicitly apply
`SetVignette(VectorSet(0, 0, 0), 1.0, 0.0)` to restore the former dark
edge effect, or animate it using the existing fade argument. This also
keeps disabled-vignette GPU work on the fast shader path.

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
depth values at or beyond the end threshold share a uniform blur zone,
including background pixels whose R16F depth is cleared to 65504.
Depth-aware filtering still protects nearer foreground silhouettes. Thus
two surfaces at depths 700 and 1000 blur together when the end threshold
is 500, instead of retaining an artificial sharp edge. Peripheral
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
modify emissive/albedo opacity handling or blend modes.

**Animation test cases:** scroll each layer separately and together, test
negative U/V speeds, ensure UVs wrap, verify `sine`/`square` timing and
`flicker`/`random` stability at different FPS, check blinking with
`emissive_factor` only (no `emissive_map`), and test both opaque and alpha-blended surfaces.

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
Materials using additive, multiply, fluid or emissive-only
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

`fluid_thickness` can be a pack-wide default overridden per material.

**Testing:** shallow/deep slopes and gaps with no underlying opaque
geometry; grazing angles; strongly reflecting normal-mapped water; opaque
versus alpha-masked albedo; fog on/off; MSAA; foreground occluders; very
high/low `fluid_thickness` and no `fluid_thickness` field. Check that
`normal`, `additive`, and `multiply` blend modes remain unchanged.

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

## Native ThingNode attachments and MAT Spotlights (experimental)

The renderer adds **`AttachThingToThingNode(child,parent,node,offset)`**
using the verified native JKGM Thing attachment, detach, update, sector and
pose functions. This is not an IJIM template-spawning verb: the child is an
**existing Thing**, retaining its sprite/3DO identity and COG controls.

```c
// Node index is the model hierarchy index, not a mesh name.
// Full animated node orientation and translation are always inherited.
AttachThingToThingNode(flare, player, 4, '0 0 0');
// GetThingAttachFlags(flare) includes 0x4 (THING), 0x8 (NOMOVE),
// and our 0x80 (NODE) while attached.
DetachThing(flare); // built-in verb also clears 0x80 and node-following
```

The child joins JK's native attached-Thing list with THING (0x4) and
NOMOVE (0x8) plus new NODE (0x80) bits, readable through stock
`GetThingAttachFlags(child)`. Full node rotation is always inherited;
only the node index and local offset live in the sidecar. Each game Thing update samples the
**animated world-space node matrix**, transforms the local offset, updates
the child's real Sith position and orientation, and uses native sector
transitions to keep it co-located with the parent. The engine's ordinary
`DetachThing` remains authoritative (via a native detach interception).
Destroyed/reused Things and level changes invalidate the sidecar. Native
hooks activate only if all required entrypoint signatures match the
tested JKGM executable. This is currently single-player experimental
code: synchronization/savegame semantics and collision effects still need
in-game verification.

Set `"enable_spotlights": true` in `jkgm.json` to enable up to **four**
COG MAT spotlights, **one per Thing**. The Thing's own Right/Look/Up
matrix defines the projector; attachment to an animated parent is now a
separate operation.

```c
// SetSpotlight(sourceThing, projectionMat, 'width height range', localOffset)
SetSpotlight(flare, flashlightMat, '3 2 12', '0 0 0');
SetSpotlight(car, headlightMat, '2.5 1.5 8', '-0.35 0.7 0.1');

// SetSpotlightIntensities(thing, light, add, fadetimeSeconds)
SetSpotlightIntensities(flare, 1.0, 0.12, 0.0); // current look
SetSpotlightIntensities(flare, 1.5, 0.05, 0.5); // fade over 0.5 seconds
SetSpotlightIntensities(car, 0.8, 0.0, 0.25); // diffuse only
ClearSpotlight(flare); // immediately remove this Thing's projector
ResetSpotlight(flare); // compatibility alias for ClearSpotlight
```

The projector's Look axis is its forward beam direction, and the offset
uses its Right/Look/Up axes. Sizes and range are measured in JK units.
The MAT's current albedo RGB and alpha form a light cookie with
soft edges and distance cutoff. The opaque depth buffer can preserve the
receiver's **unlit albedo** (in its GBA channels) when spotlights are
enabled. Spotlights light that pigment with a screen-like diffuse term
plus a separate additive cookie contribution, even if the previously shaded
surface was black. Each projected beam now has conservative screen-space
bounds, so the compositor skips expensive projector and normal sampling
outside the beam footprint without scissoring away fog or SSAO. Fully
offscreen beams are culled. Light attenuation starts gradually at 35% of
the requested range instead of falling away only in the final 15%;
the beam still reaches zero at the same specified range.
**SetSpotlightIntensities** independently scales the
two components: `light` illuminates the receiver's unlit pigment,
`add` overlays unmodulated cookie radiance. Their defaults are **1.0**
and **0.12**, preserving existing Spotlight visuals. Both accept
strengths from 0 to 16 (not restricted to 0–1); `fadetimeSeconds`
is 0 for immediate changes or up to 3600 for linear interpolation.
Repeated calls start from the currently interpolated values, and
`SetSpotlight` on the same Thing preserves an active fade.
`ClearSpotlight(thing)` removes the registered projector immediately
without releasing its MAT; `ResetSpotlight` is the same operation.
`RemoveSpotlight` was never a registered COG verb. Call
`SetSpotlight` first; the intensities verb requires an active Spotlight.
These contributions affect **opaque world and 3DO receivers** through
the shared depth/normal buffer, independent of model caching. Transparency, shadows, specular lighting and volumetric beams
are not yet implemented. The first version evaluates four lights
fullscreen, so performance should be measured before increasing that cap.

**Native MAT preload (experimental):** SetSpotlight attempts to load
its MAT's currently selected cel, using the verified JKGM
`rdMaterial_AddToTextureCache` entrypoint at `0x0044ACC0`, **only if
the MAT is not already resident**. Native calls are no longer retried
or used to refresh the texture-cache LRU during frame rendering: JK's
pause/unpause DirectDraw transitions can leave native std3D internals
uninitialized, and those calls caused a null-pointer write in the native
uploader. If a projector MAT is missing/evicted later, the spotlight
temporarily does not draw and a warning requests that its MAT be loaded
normally. Reissue SetSpotlight only after the game graphics are stable,
or arrange for the MAT to be rendered on native geometry. This conservative
change prioritizes avoiding crashes over unconditional offscreen MAT
residency; a safer deferred asset-loading route may be introduced later.
The function,
its original native callsite, and the D3D upload target are all checked
against the audited executable's instruction bytes before any call;
unknown executables decline the native preload. This is distinct from
the **incorrect** `0x0044AA70` address, which previously crashed Wine.
The new native route is statically verified but must still be tested
in-game before treating it as stable.
To recheck the original binary:
`python3 tests/verify_spotlight_loader_binaries.py /path/to/jkgm.exe`.

Type-4 camera-facing flare SPRs are separate Things. Attach a flare
Thing to a node, then assign that same Thing a Spotlight. Its independent
`SetSpriteInfo` and `SetSpriteRoll` COG controls remain available.

## Experimental spotlight shadows

Set `"enable_spotlights": true` and `"enable_spotlight_shadows": true`
to test **one** 256×256 depth-only spotlight shadow map. Shadow rendering is
off by default. The ordinary `SetSpotlight` verb works unchanged; the first
on-screen ready projector gets the shadow-map budget. Other projectors remain
unshadowed.

The static SithWorld caster mesh reuses the level-surface cache and BVH.
Shadow rendering tests the projector volume and submits only overlapping
indexed geometry. World architecture outside the player's camera view remains
available to cast shadows. Unchanged static shadows reuse the previous depth
map without redrawing the world. Dynamic 3DO mesh ranges use their existing
cached vertex/index data and updated Thing/node world transforms.

**Native renderer compatibility:** with `"use_model_cache": false`, and
shadows enabled, the verified native rdModel3 mesh hook copies bounded,
near-spotlight geometry into a shadow-only cache but still lets JK draw the
actual 3DO. This is not the GPU-cached model rendering option and is not
enabled when shadow maps are off. With `"use_model_cache": true`, the
existing indexed buffers are reused for rendering and shadows.

**Current behavior and limitations:**

- The shadow-only world cache retains *initially hidden* polygons, but the
  capture checks each candidate's **live surface geomode** before drawing.
  This makes COG geomode 4 → 0 and 0 → 4 changes take effect without
  rebuilding the level mesh. The current geomodes enter the depth-map cache
  signature so stationary lights don't reuse stale shadows. World surface
  normals provide one-sided light-facing rejection; 3DO draw ranges match
  the normal renderer's backface/double-sided and mirrored-winding policy.
- A spotlight origin inside a 3DO group's world-space bounds excludes that
  group from the shadow caster pass (common for vehicle headlights). This
  does not change shadows cast by the rest of the scene; lights truly buried
  behind a different visible opaque surface may still require an offset.
- Projected SPR decals reject oppositely facing back sides of slabs and
  draw one-sided in the cached GPU renderer. Perpendicular/adjacent surfaces
  can still receive finite-volume projection; no strict corner angle limit
  has been reintroduced.
- World casters now retain checked native MAT UV indices, cel and texture
  offsets. Ordinary opaque surfaces remain a single batched indexed draw;
  alpha-cutout world faces (grates, fences, masks) sample their current
  texture alpha in the depth shader so holes transmit light. The same
  mechanism handles opaque/masked GPU and native-captured 3DO faces.
  Unresolved or invalid UV/textures fail open instead of casting solid
  rectangle shadows. The `alpha_cutoff` material setting is honored for
  masks; mixed native alpha casts only its nearly opaque texels.
- The selected shadow map is reused for both opaque and forward-blended
  receivers. It shadows the **added** spotlight contribution, not JK's
  original baked lighting, ambient or emissive surfaces. Additive/multiply,
  post-exempt and decal material policies are unchanged.
- Shader source includes simple alpha transitions and UV scroll values.
  Dithered shadow casters currently use a stable threshold instead of
  their visible 4x4 Bayer coverage. Fully transparent glass does not cast
  colored transmission shadows.
- 3DO shadows include meshes reached by JK's usual rdModel3 hierarchy.
  If the engine eliminates an entire model before invoking its mesh hook,
  it cannot cast shadows from fully outside the camera view. An
  independent Thing-level collector remains a possible extension.
- An unchanged light plus unchanged participating 3DO material/transform
  signature reuses the depth map. A 256px resolution, one-shadowed-light
  limit and four PCF taps are the deliberate low-cost defaults.

This feature still requires in-game testing on native and cached 3DOs, and
may be disabled independently without affecting projected spotlight lighting.

## Forward projected lighting on translucent receivers (experimental)

The existing COG MAT spotlights also illuminate **genuinely alpha-blended**
world polygons and GPU-cached 3DO faces. No new framebuffer, depth copy, or
scene draw is required: the original projected-cookie data is evaluated
directly during the existing forward-alpha pass. Screen bounds are checked
before world-position reconstruction; with `enable_spotlights: false` or
no active lights the shader immediately skips this code.

- Native world transparency and GPU-cached alpha-blended models share the
  same position, incidence, cookie, attenuation and light/additive intensity
  behavior. Lights respect the material's alpha and fog.
- Alpha-mask and ordered-dither coverage already lights through the opaque
  compositor and is *not* relit. Ordinary additive, multiply, emissive-only,
  flare, projected-decal and post-exempt/weapon rendering remain unchanged.
- Fluid transparency may receive projected illumination without making
  shallow water suddenly opaque; its illumination is handled independently
  of its Beer–Lambert albedo absorption.
- Existing projectors occupy texture units 12–15 during the alpha stage;
  all previous material samplers retain their bindings. This requires the
  GLSL 3.3 minimum of 16 fragment texture units. Projected lighting on
  translucent faces is **not shadowed yet**, and does not calculate glass
  light transmission or refraction.

Verify on alpha-blended glazing, water and fading sprite geometry with the
same `SetSpotlight` values used for opaque surfaces. With spotlights off,
appearance should be unchanged, including masked foliage, HUD and decals.
No new COG verbs are needed. The feature remains experimental until native
and GPU paths are checked in-game.

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
| `SetGlobalEnvMap(sourceMat, imageID, fadeTime)` | EnvMap from source MAT's pack on all replacement materials without an explicit EnvMap, across packs; does **not** override individual material reflection strength or roughness |
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
has not yet been uploaded. The renderer preserves the previous valid EnvMap
(or the material's default) while a requested source image cannot be resolved,
instead of clearing every EnvMap on a failed global image lookup. The requested
image becomes active once its pack mapping and image ID are available.
World changes clear commands to avoid handle reuse.
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

## Distant geometry and HOM

`retain_hom_pixels` uses G-buffer normal alpha as an explicit opaque-pixel
coverage signal instead of treating large linear-depth values as empty space.
Large skyboxes and other distant opaque geometry can now replace retained
background pixels even when half-float depth has saturated. Distance-dependent
effects past roughly 2047 world units may still need a separate extended-range
depth representation; this change intentionally avoids doubling depth texture
bandwidth. Camera HUD texture placeholders are suppressed during menu swaps.

## GPU sprite cache eligibility diagnostics

`PERF sprite_gpu_reject_avg` reports average native fallbacks by reason:
source read, face geometry/light/texture mode or flags, resource UVs,
camera pose and cache capacity. The first eight distinct
`SPR GPU face sample` lines print native mode numbers. These bounded
diagnostics run with `profile_renderer` even when verbose tracing is off.

## Profiling and diagnostics

`"profile_renderer": true` continues to report aggregate frame, GPU, native
and sprite performance. Optional `"profile_renderer_verbose": true` restores
per-Thing sun/RLU, shadow atlas, SPR pose and decal receiver diagnostics and
individual GPU 3DO fallback names. Verbose diagnostics default to false, so
log writes do not skew normal A/B performance runs. Repeated texture upload
signature and material-map debug messages are no longer emitted.

## GPU-cached ordinary sprites (experimental)

Set `"use_sprite_cache": true` to test shared GPU quad geometry for ordinary
type-0 camera-facing, type-1 cylindrical billboard and type-2 fixed-plane SPRs.
Fully-lit geometry-mode 3 (solid MAT palette color) and mode 4 (textured MAT)
sprites qualify. Mode 3 uses MAT solidColor without UVs or texture uploads.
Native texture-mode values 0/1/2 are ignored for GPU eligibility: they only
control the software renderer's affine/perspective sampling. Nonzero sprite
lighting modes, decals, flares and unavailable MAT colors/textures retain JK's
native renderer.
Shared geometry eliminates duplicate quad uploads; **hardware-instanced draws
are a separate next step** and are not yet implemented. The option defaults to
false for A/B testing against the native sprite path.

## Filled 3DO modes and GPU caching

GeoMode 3 (solid color) and 4 (textured) are both GPU cache candidates.
Effective Thing/mesh GeoMode 3 forces a solid draw regardless of an individual
face's texture settings; individual mode-3 faces also render solid within
mode-4 meshes. The solid RGB is resolved from the MAT cel's palette-indexed
solidColor and the appropriate current or identity colormap. Native lighting
continues through the cached shader. GeoModes 0..2 retain native rendering.
Mesh and face TexMode values do not affect GPU cache eligibility or
source-signature invalidation; OpenGL interpolates UVs perspective-correctly.

## Camera Things as unlit 3D HUD objects (experimental)

Use `type=camera` (SithThing type 1) together with an ordinary `model3d`
on a Thing intended to be part of a 3D HUD. This is distinct from the
active game camera. The Camera Thing's `thingflags` field selects two modes:

- `type=camera` without ThingFlag `0x20`: **always-on-top HUD**, ignoring world occlusion.
- `type=camera thingflags=0x20`: **world-anchored GUI** (nametags, target
  indicators, objective markers), hidden behind opaque scene geometry.

Ordinary non-Camera Things with `0x20` (including skyboxes) retain their
light/shadow-exemption behavior and normal post-processing. The flag alone
never causes late HUD rendering.

On the GPU-cached 3DO path, all faces of a Camera-type Thing are rendered
in the final overlay **after** bloom, HDR, depth/peripheral blur, noise,
vignette, grading, Retro quantization and frame-trail history. This HUD
rendering ignores JK vertex lighting, the active colormap and native
per-camera color filter, scene fog, SSAO, sunlight, shadow casting,
reflections and material specular/emission. The albedo texture, MAT
alpha/mask and normal/additive/multiply blend modes remain available.
The default Camera HUD draws over world geometry. Camera Things carrying
`0x20` instead compare each fragment against the resolved opaque view-depth
buffer from the same frame: solid walls and opaque 3DOs occlude them, but glass,
alpha-blended sprites and other transparent surfaces generally do not.
Both modes draw before the traditional DirectDraw 2D HUD and after postFX.

The HUD classification and optional depth mode belong to each **Thing instance**,
not its mesh or MAT. The same 3DO and MAT used on a normal Thing render normally
in the world. No replacement-material metadata is required, and the Camera layer
also works when `enable_post` is false. The GPU model cache is the intended
path. An unsupported 3DO or unready native MAT may temporarily use the
original D3D renderer, so `profile_renderer=true` logs
`Camera Thing HUD 3DO native fallback` to identify incomplete coverage.
No hard-coded model, mesh or MAT names are used.

## Material depth bias for overlapping model surfaces

A replacement material may specify `"depth_bias": 1.0` in its
`metadata.json` entry (default `0.0`, valid range `-16.0` to `16.0`).
Positive values favor that native-rendered surface in depth tests by
pulling its depth slightly toward the camera; negative values push it
away. This uses raster `glPolygonOffset` (slope-aware factor and depth
units), **not** a geometric displacement, and restores the standard GL
state at the end of each native triangle batch. Eye planes behind
alpha-masked eye sockets are an intended use case: start at 0.5–1.0 and
increase cautiously. Large biases can pull eyes visibly through the
face at grazing angles and cannot correct geometry silhouettes or
transparent face sort order. The initial implementation only affects
native JK triangles; the experimental GPU-cached 3DO path is unchanged.

## Spotlight MAT residency

Once a projector cookie MAT has reached JKGM's GPU texture cache, the
renderer retains an extra reference to its OpenGL image. JK's native Direct3D
LRU may evict or reuse the original surface without invalidating the
projector image. The held reference is released when no active spotlight uses
the MAT, when the world changes, or when the renderer shuts down. If a
projector first activates *before* its MAT has ever been uploaded, the
normal native SetSpotlight-time upload/readiness restrictions still apply.
No native MAT loader calls are made per frame, preserving the pause/unpause
crash fix. A cached cookie may keep showing the last loaded cel while a new
cel remains non-resident; once JK uploads the new cel, the hold updates.

## Fast compositor presentation

When both temporal trails (`enable_trails`) and Retro CMP quantization are off,
the final color/post-FX shader now renders directly to the sRGB backbuffer,
skipping a redundant fullscreen history write and copy. Enabled bloom, film
grain, exposure and color filters still use their original shaders and order.
The direct path explicitly invalidates old trail history, so re-enabling trails
cannot blend a stale frame. With trails or Retro enabled, the original
two-stage history/quantization path is preserved. The non-bloom compositor
also skips sampling unused bloom maps, and disabled vignette avoids expensive
per-fragment distance calculations. Check `PERF present_gpu_us compositor=`
before/after in the same scene; an FPS gain is not guaranteed if the game is
CPU-bound.

## Unprocessed menu presentation

Menu surfaces now bypass gameplay post-processing entirely, including
8-bit/GDI pause menus that are presented while the display mode still reports
gameplay. The fullscreen menu-present shader derives its texture coordinates
from the fullscreen quad positions (the ordinary menu shader expects explicit
UVs, which caused a blank startup menu). JK's palette-based menu texture is
still decoded as sRGB and presented
through the normal linear-to-sRGB framebuffer conversion, but menus
skip bloom, exposure, grading, film grain, vignette, spatial blur,
and frame-trail accumulation. Gameplay post settings and their
COG fades are not reset by showing a menu. The menu also skips the
bloom compositor's intermediate render targets, rather than just
zeroing the bloom weights.


### Native transparent triangle sorter: bounded worst-case

Translucent native triangles continue to use the original depth/plane-aware
geometric partitioning in ordinary cases. Very overlapping or coplanar
surfaces could previously trigger deeply recursive full-range sorts without
making progress. The sorter now limits the total classified elements per
batch to 98,304 and recursion to 48 levels, with at most eight consecutively
unproductive pivots. Only a partition that exhausts one of these limits is
ordered approximately by eye-space W (matching the existing near/far sign);
other partitions retain their geometric order. Alpha-masked depth-writing
triangles preceding the blended range are never affected.

With `"profile_renderer": true`, the `PERF transparent_sort_fallback` line
records the fallback partition count, affected triangle count and frames
requiring a fallback. Occasional nonzero values are expected when strongly
overlapping transparent faces become ambiguous. If a particular visual effect
sorts incorrectly, capture the scene and profiler line so we can refine that
case without restoring unbounded recursion.

## Audio diagnostics: Aureal A3D and Wine (read-only)

To investigate why the JK sound menu does not expose (or does not
successfully use) Aureal A3D, add these settings to `jkgm.json`:

```json
"profile_audio": true,
"log_path": "jkgm_audio.log"
```

The renderer logs `AUDIO modules:` at most once per 1.5 seconds,
**only when the module set or audited engine state changes**, in both game
menus and gameplay. It records the paths of loaded `dsound.dll`,
`a3d.dll`, `a3dapi.dll`, `a3dsnd.dll`, `dsoal-aldrv.dll`,
`openal32.dll` and `winmm.dll`, plus whether the *already loaded*
`a3d.dll` exports `DllGetClassObject`.

On the matching, signature-verified standalone JKGM executable it also
reports an `A3D engine state` snapshot:
- `sound_device`: the interface JK uses after either the A3D path or
  fallback `DirectSoundCreate` (nonzero does **not** establish A3D).
- `a3d_path_flag`: JK's indicator from its A3D-specific initialization
  path. The initializer clears this flag on several failures.
- `a3d_secondary`, `primary_buffer`, `ds3d_listener`: additional
  interface pointers reached during JK's sound initialization. The
  `ds3d_listener` interface at `0x00563760` is explicitly requested as
  `IDirectSound3DListener` on the primary buffer. Nonzero means an interface
  pointer was stored, not that hardware/HRTF works.

The snapshot also reports `listener_input_camera`, `sound_slots`,
`listener_input_pos`, `listener_input_front` and `listener_input_up`
when the executable's native audio-tick code signatures match. These are
the original camera/listener inputs JK prepares for its listener-update
routine, **not** evidence that an update call executed.

It now additionally queries the *existing* DirectSound3D listener's
`GetAllParameters` method when the method's vtable entry is verified to
belong to the already loaded `dsound.dll`. This is a read-only COM
operation, not an audio hook or a setter. The resulting `ds3d_actual_pos`,
`ds3d_actual_front`, and `ds3d_actual_up` show what DirectSound currently
holds. The position may differ in scale from `listener_input_pos`:
JK converts world units to its sound-space units before sending them.

Walk and rotate the camera. If both input and actual values change,
listener position/orientation reaches DirectSound. If only inputs
change, examine the native listener-update call path. If the readback
reports `unverified-dsound-method` or `invalid-vtable`, **do not**
interpret that as a frozen listener; the diagnostic deliberately skipped
an unsafe or unverified call.

The sound-source probe, which also requires signature-verified JKGM
sound-pool layout, records `allocated_sounds`, `live_sound_buffers`,
`live_ds3d_sources` and `queried_ds3d_sources`. Up to three currently
live sources include `ds3d_source[N]=(mode=...,pos=...)`. Mode 0 is
normal positional audio, 1 is head-relative, and 2 disables 3D processing
for that buffer. The getter is only invoked if the source interface's
method is verified to reside in Wine's loaded `dsound.dll`.
An empty sample is not proof of failure if no live positional sounds
were present during the short polling interval.

All probes require `profile_audio` and the audited executable signature.
They do not load additional libraries, create devices, modify audio
settings or install hooks. The guarded COM getters only retrieve existing
listener/source state. Unknown executables report
`unsupported executable/signature` and avoid these engine/COM queries.

For a useful A/B test, keep the same Wine prefix and DLLs, record one run
with ordinary sound and another after selecting A3D in JK's sound menu
(if selectable). Compare the `a3d_path_flag` and interface pointers.
A DLL loading or even an A3D flag of 1 cannot establish actual positional
HRTF. If the flag stays 0, inspect the **exact** game-local `a3d.dll` to
find out whether its COM class factory is actually compatible with JK;
do not blindly install DLL wrappers into a working game.

A separately packaged 32-bit `audio_probe.exe` tests whether the Wine
prefix can create a DirectSound device, obtain device capabilities, and
create/query a mono DirectSound3D buffer. It does not play sound or change
registry settings. Put it in the same directory as the game executable
and run it using the **same Wine prefix** as JK:

```sh
WINEPREFIX=/path/to/your/prefix wine audio_probe.exe > audio_probe.txt 2>&1
```

`DirectSoundCreate=OK` is not sufficient by itself: A3D is a separate
legacy API/compatibility layer, and a DirectSound3D-capable driver does
not guarantee JK recognizes the A3D device. To see whether Wine chooses
builtin or native audio DLLs, launch JK once with:

```sh
WINEPREFIX=/path/to/your/prefix WINEDEBUG=+loaddll wine jkgm.exe > wine_audio_load.log 2>&1
```

Keep your exact existing launch arguments/environment when testing. On
Wine, a native `dsound.dll` or Aureal compatibility wrapper requires
careful DLL-overrides configuration; do not copy unverified A3D wrappers
into the prefix, because incompatible wrappers are known to break JK
in-game sound effects even if its menus still work. Compare normal
playback, A3D menu availability, and audible front/back positioning.

Neither diagnostic changes the audio backend. Future work can trace
actual DirectSound/A3D buffer creation and COG sound-handle behavior
after the loaded DLL chain is understood.

## High-resolution MAT UVs on GPU-cached 3DOs

The community high-resolution MAT patch preserves a 256x256 maximum UV
reference area for **model geometry**, regardless of the texture's actual
resolution. GPU-cached 3DOs now follow native JK: each UV axis is divided by
`min(native MAT mip-0 width/height, 256)`, not necessarily by the full texture
dimensions.

For example, a quad with U and V spanning 0..256 tiles a 64x64 MAT four
times per axis, a 128x128 MAT twice per axis, and any native MAT of 256px or
greater once per axis. 512x256 and 1024x2048 textures also fill that UV square
once in both directions. Smaller rectangular MATs repeat independently in
each dimension. This applies to the base albedo and material detail maps,
because all GPU 3DO shader sampling shares these UV coordinates.

The dimensions come from the *native MAT cel*, not the resolution of an
optional replacement PNG: replacing a 64x64 MAT with an HD material does not
automatically change the model's intended UV tiling. The fix is draw-time
only and does not rewrite model geometry, affect native 3DOs, world surfaces
or the separate projected SPR decal UV path. A Blender exporter authoring
change can follow later, so exports present the same 256-based convention.

## MAT residency and native fallback

Direct native MAT preloading from GPU-cached 3DO interception has been
**removed** after it caused a page fault in JKGM's std3D uploader during
level loading. Older configs containing `"gpu_mat_preload": true` are
accepted but ignored with a warning. When a GPU-cached 3DO references an
unloaded MAT cel, the renderer yields to native JK's normal mesh draw and
automatically uses the GPU path once the texture becomes resident. No
special switch is necessary.

The separate explicit `SetSpotlight` MAT request remains available:
its loader is *not* invoked from the per-frame GPU mesh interception path.

## Shared native MAT readiness checks

GPU-cached 3DO meshes and GPU SPR decals now share confirmed native MAT/cel
residency results within a frame. Previously, each mesh repeated the same
native texture and mip-surface validation for every material reference.
The shared cache rechecks the active cel for animated materials, discards
results at the next frame, and invalidates on D3D texture uploads. Missing
cels are **never** memoized, so JK can warm them up in the same frame via
native drawing. This is an incremental CPU optimization only: it does not
yet directly upload MAT pixels without JK, and does not change how materials
are decoded or rendered.

With `profile_renderer`, the `PERF gpu_3do readiness` line now includes
`mat_cel_refs_avg` (references examined) and `shared_resident_hits_avg`
(cache hits) next to `readiness_us_avg` to measure the savings.

## Optional CPU/geometry frame profiler

### Native sprite pipeline profiling (SPR GPU batching baseline)

The experimental native SPR profiler is available when `"profile_renderer": true`
and the verified JK23 sprite detour is attached. It adds two `PERF
native_sprite` lines every ~2 seconds:

- `type0/1/2/3/4`: authored sprite draw-hook calls per frame; `other`
  includes missing type metadata and unsupported types. `hidden` counts
  calls skipped due to zero size/opacity.
- `draw_us_avg`: inclusive CPU time per frame in JK's real
  `rdSprite_Draw` function, excluding GPU decal projection and OpenGL work;
  `rdSprite_Draw_calls_avg` and `returned_nonzero_avg` show how often the
  native path was invoked and returned successfully.
- `proc_faces_avg`: native SPR faces successfully submitted to
  `rdCache_AddProcFace` (available only with the exact-alpha hook).
- `projected_gpu_avg`: type-3 sprites that successfully queued cached GPU
  decal geometry. `projected_native_warmup_avg`: masked native fallback
  draws used to make decal MAT textures resident.

Use a particle-heavy level and compare these lines with `PERF execute_buffers`,
`PERF sorting_us`, and `PERF geometry`. They establish whether native sprite
generation, converted triangles, or OpenGL material/draw overhead dominates.
The profiler changes no rendering behavior; ordinary SPR types 0-2 and
center-occluded type-4 flares still use JK's native quad submission.
The planned persistent GPU quad path must preserve per-Thing sprite orientation,
per-MAT cel loading and transparency sorting before replacing that submission.

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


### Native-path presentation profiler (CPU + asynchronous GPU)

With `"profile_renderer": true`, the existing inclusive `PERF passes_us
post_swap` timer is additionally split into:

- `PERF present_cpu_us resolve`: end-of-frame MSAA/color resolve;
  `blur`: optional depth/peripheral blur;
  `retro`: Retro CMP palette verification/update;
  `compositor`: auto exposure, bloom, final post-processing;
  `swapbuffers`: `SwapBuffers`, which can include driver waiting;
  `beginframe`: start next render frame.
- `PERF present_gpu_us resolve/blur/compositor` uses OpenGL timestamp
  queries, buffered eight frames. Query results are **only read after the
  final timestamp is reported available**; no glFinish or blocking
  query-result polling. `samples` counts completed prior frames, and
  `ring_busy_skips` counts unsampled frames when results are still pending.
  A zero GPU time with zero samples is *not* a measured zero.
  SwapBuffers waiting is CPU-side and does not have its own GPU timestamp.

These measurements separate CPU submission/wait from queued GPU work.
`present_cpu_us` timings are children of `post_swap`; do **not**
add them to it. GPU work may overlap CPU work and should not be added to
CPU timings to estimate a frame period. The existing native 3DO timers
are inclusive too.

`"profile_auto_exposure": false` (default) disables the old periodic
debug `glReadPixels` that previously ran automatically whenever
`profile_renderer` was enabled. Set it to true only to inspect
exposure EV/luminance; the readback synchronizes with GL and can distort
timings. For A/B comparisons, keep VSync off, settings/resolution and
camera view identical, and disable both profiling options for final FPS
measurements.

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


### Neutral color grade and native-path HUD upload optimization

The default post-process `SetColorFilter` grade is now the identity:
brightness `0`, contrast `1`, saturation `1`. Level scripts can select
their preferred grade using `SetColorFilter(vector, fadeTime)`, and
`ResetColorFilter()` restores the neutral values.

The replacement renderer's DirectDraw HUD overlay (shared by native and GPU
3DO rendering) still uses the original four-frame double-buffer update cycle,
but `"optimize_hud_uploads": true` (default) compares each slab against a
CPU-side mirror of its **actual destination texture**. It skips unchanged
slabs and uploads only the contiguous rows containing changes. It never
alters geometry or shading and always clears/swaps the HUD at the same time.
Set `"optimize_hud_uploads": false` for the previous full-slab behavior.
With `"profile_renderer": true`, compare the `PERF passes_us HUDupload`
value in identical locations. Note that this category includes both
CPU RGBA conversion and the GPU driver call, and does not measure GPU time
independently.

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

Set `"use_model_cache": true` in `jkgm.json` to draw **all supported
filled 3DO meshes** from cached GPU vertex/index buffers. Set `false`
(default) to use the previous JK-assisted 3DO renderer, while retaining
JkGRfxMod's normal OpenGL world/sector rendering. This is not
`use_vanilla_renderer`, which disables JkGRfxMod entirely.

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

Emissive-only replacements with albedo factor `0 0 0 0` preserve the
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

The new path requires the updated `renderer.dll`, plus
`gpu_3do_preview.vert` and `gpu_3do_preview.frag` from the same
build in `jkgm/shaders/`. Keep the native `jkgm.exe` bootstrap.



### Experimental mesh revision tracking

With `"use_model_cache": true`, set `"mesh_revision_tracking": true` to
enable experimental JK.dll COG mutation tracking. The renderer wraps 16
mesh/material mutator verb registrations, preserving the original COG handlers
and results. A mutation bumps a global generation: cached 3DOs only need a new
source-byte signature when the generation changes, their mesh header changes, or
once every 32 frames for an integrity audit. An audit mismatch permanently
disables the shortcut for that session and restores original per-frame hashing.
If the expected JK.dll registrations are missing, full hashing remains active.

The setting defaults to false. **This is not yet a guarantee of instant
invalidation for writes originating outside the known JK.dll verbs**; those
can remain stale until the next audit. Texture pixel uploads/residency and
per-Thing lighting are separate from geometry revision tracking. Benchmark
with `"profile_renderer": true` and compare `PERF gpu_3do source_check_us_avg`,
`checks_avg`, and `invalidations_total` with the setting off/on. For
compatibility tests, exercise CloneModel, SetModelMesh, ReloadModel,
ReplaceModelMaterial and ReplaceModelMeshMaterial, as well as animated cels.

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

## KEY animation marker `callback` COG message (JK23)

JkGRfxMod now forwards KEY marker events from a Thing's playing
animation track to a `callback:` COG message. The Thing is already available
as `GetSenderRef()`, so only marker value and animation track are parameters:

```c
callback:
    eventThing = GetSenderRef(); // animated Thing
    marker     = GetParam(0);    // marker value from the KEY
    track      = GetParam(1);    // playing rdPuppet track index
    return;
```

The message is delivered to the Thing's COG, capture COG, and linked COGs
through the native Thing-message dispatcher. Existing marker behavior such as
footsteps, firing and track-end callbacks is preserved. Marker 0 (track
completion) is forwarded too. The implementation temporarily installs a
marker relay while a track advances, including when its native callback slot
is empty; it does not poll animation progress or duplicate KEY data.
The extended message is registered through the JK23 `JK.dll`
`InitNewCogMessages` extension point. Native signature checks occur *before*
Detours patches the executable; repeating them during message registration
would incorrectly reject the modified function prologue.

This feature is **specific to the tested JK23 EXE and JK.dll**. Native
signature validation disables it when addresses or layout do not match.
Use `callback:` in a linked COG, not as a callable COG verb.

## JK23 AI introspection COG verbs

JkGRfxMod adds three **read-only** getters for AI-controlled actors.
They use native JK23 AI controller fields and do not replace the original
AI logic or alter any animation/instincts:

```c
thing  fireTarget = AIGetFireTarget(actorThing); // -1 if unavailable
vector moveGoal   = AIGetMoveTargetPos(actorThing);
vector lookDir    = AIGetLookDir(actorThing);
```

- `AIGetFireTarget` returns the Thing reference last supplied to
  `AISetFireTarget`. This is the controller's `pDistractor`, not a promise
  that attack instincts will actually fire on this frame.
- `AIGetMovePos` **overrides JK's original getter** to return the
  *commanded* movement destination at `SithAIControlBlock + 0x1A0`.
  `AIGetMoveTargetPos` remains an alias. JK's original getter instead
  read the AI's sight/search position at `+0x218` (also consumed by
  `sithAI_sub_4EAF40`), which is not the movement target. This deliberate
  override changes a native verb's semantics; the correct name binding and
  gameplay behavior still need verification in JK23.
- `AIGetLookDir` reads the existing horizontal normalized `goalLVec`
  at `+0x194`. The engine does not retain the original absolute
  `AISetLookPos` argument after converting it to this direction.
- The position/direction getters return `(0, 0, 0)` for invalid or
  non-AI Things, and the fire-target getter returns `-1`.

### Why `aievent:` rarely fires in JK

`aievent` is built-in COG message **24**, not a catch-all for the
native AI's internal instinct events. In the JK AI event emission routine
`sithAI_EmitEvent`, the COG message is sent for **AI mode changes**,
with `GetParam(0) = 256` (`SITHAI_EVENTMODECHANGED`). The internal
event argument is otherwise dispatched to registered AI instincts without
sending a COG `aievent` message for each event. It is routed via the
normal Thing-message system (class, capture and eligible linked COGs).

The native handler first checks actor/class existence, health, death,
disabled/sleeping state and related conditions. Therefore, a script cannot
assume every target sighting, movement, or attack dispatches `aievent:`.
This behavior has been checked against the user's JK23 executable and
the available OpenJKDF2 reconstruction; gameplay verification is pending.

## SPR types and new rendering modes (JK23)

The SPR `type` field is a **mutually exclusive mode**, not a set of
bitwise flags. Its values are ordinary **decimal integers**, parsed by the
original loader with `atoi()`. The JkGRfxMod-only modes are:

| Type | Rendering |
|---|---|
| `0` | Unmodified JK camera-facing sprite (standard) |
| `1` | Upright/cylindrical billboard (preserves Thing Up) |
| `2` | Fixed 3D plane (preserves Thing R/L/U) |
| `3` | Projected surface decal (fixed projector basis) |
| `4` | Center-occluded flare (camera-facing) |

No combinations are defined or supported. Values `5`–`127` are
reserved and currently use the ordinary camera-facing fallback.
Previously used experimental bit values `0x20` and `0x40` are obsolete;
change those SPR definitions to **decimal 3** and **decimal 4** respectively.
Types `0`–`2` remain valid without any executable patch; the extended
JK23 executable remains capable of loading values `0`–`127` and doesn't
need another patch for this change.

## Automatic HDR eye exposure (experimental)

The HDR display transform and automatic eye adaptation are separate. For a
starting point with restrained dark-scene adaptation, use:

```json
"enable_hdr": true,
"auto_exposure_sensitivity": 0.2,
"hdr_exposure_compensation": 0.5
```

- `enable_hdr` (default `false`) is the **strict master switch** for HDR
  tone mapping, exposure compensation, and automatic eye adaptation. When
  false, every other HDR setting is ignored (including positive
  `auto_exposure_sensitivity`). HDR is also bypassed when
  `enable_post: false`, in menus, and for Retro CMP output.
- `auto_exposure_sensitivity` (0..2, default 0) enables scene-adaptive
  exposure only when `enable_hdr: true`; at zero, HDR uses fixed exposure.
  The meter now considers localized bright patches, so a flashlight on a
  dark wall can lower exposure and deepen other, dimly lit parts of the view.
  Isolated hotspots are capped to avoid large exposure jumps.
  Dark-scene boosts remain capped at +1.25 EV; bright-scene compensation
  supports up to -5 EV. Adaptation is faster when entering bright light.
- `hdr_exposure_compensation` (-3..+3 EV, default +0.5) is a **fixed**
  brightness adjustment added to any adaptive exposure before tone mapping.
  Increase this to lift visible shadow detail without increasing the
  adaptation strength. A strictly black input stays black.

The GPU-only 1x1 RGBA16F pass meters 144 samples in the central 80% of
the scene, combining log/linear averages with a capped RMS highlight meter.
The shadow toe (`hdr_shadow_softness`) and color mapping
(`hdr_color_crispness`) can be adjusted independently between older
crisper and newer hue-preserving styles. Bloom is extracted based on the
same exposed brightness as the final image, then added in HDR before tone
mapping, not double-exposed. Tone mapping precedes COG color grading and
optional grain/vignette.

`profile_auto_exposure: true` enables a blocking diagnostic readback every
two seconds (meter luminance, capped highlight luminance, adapted and
target EV, sensitivity and fixed compensation). Leave it off for frame-rate measurements. The exposure history
is reset for menus, disabled postprocessing or Retro 256-color mode. Metering
still includes center-screen HUD elements; bright/dark input calibration
remains material-dependent. This feature does not change native JK vertex
lighting limits.

## SPR projected decals — geometry and receiver discovery

SPR type **`3`** currently supplies a fixed projector basis and
`renderer/sprite_decal_clip.hpp` clips convex receiving polygons against
six oriented projector planes. UVs derive from SPR Right/Up orientation;
back-facing, degenerate and out-of-range surfaces are rejected.

`renderer/sprite_decal_world.hpp` adds **bounded JK23 SithWorld surface
extraction**, reading the native world vertex index arrays and rejecting sky,
passable portals, untextured and non-facing surfaces. Solid textured adjoins
(including grates) remain eligible. It produces triangulated decal
geometry with surface IDs and projected UVs. This extraction uses an
injectable, read-only memory reader for safety and isolated testing.
The world/surface offsets after the verified world header are based on
OpenJKDF2 and **must be checked in the executable before runtime use**.
Tests use synthetic in-memory JK structures and cover adjacent surfaces,
invalid indices, backfaces, sky/portal exclusions and output caps.

`renderer/sprite_decal_projector.hpp` builds a projector from the Thing's
current position and R/L/U axes, the SPR's local offset and base dimensions,
and independent `SetSpriteInfo` size / `SetSpriteRoll` values. Projection
depth is fixed at 0.25 engine units; receiver triangles are offset a
hardcoded 0.001 units toward their front side to avoid z-fighting without
changing texture coordinates. These values are experimental, not COG knobs.

**Runtime diagnostic bridge (opt-in):** with renderer profiling enabled,
the first draw of each type-`3` SPR resource per world builds the current
Thing projector and makes a **read-only** attempt to clip the live JK23
SithWorld's opaque surfaces. The log prints `SPR decal probe` with
`valid_world`, surface/receiver counts, triangle counts and errors.
The probe also reads the **original SPR material and four MAT UV corners** and
attempts to build an indexed GPU-compatible mesh. Check `mat_valid=1` and
`gpu_mesh_ready=1` in addition to `receivers`/`clipped_tris`.
A zero receiver count is expected if the projector points away from nearby
opaque architecture. The scan does not run in non-profiling gameplay.

Type-`3` SPR decals automatically use resolved opaque scene-depth clipping
(fixed 0.15 JK-unit plane-gap tolerance). No `depth_clip` material JSON
setting is needed or supported. Alpha-masked grates contribute depth only
at opaque pixels; transparent holes remain empty. Fully alpha-blended
receivers still lack usable opaque depth and cannot receive decals.

**Receiver lighting and translucency (experimental):** projected SPRs use the
opaque G-buffer's JK diffuse-light value, plus the same SSAO factor as the
receiver, instead of remaining fullbright in dark sectors. The light value
occupies the previously unused **alpha** of the existing RGBA16F emissive
target; the opaque compositor still uses its original RGB and no additional
texture or fullscreen pass is allocated. This transfers diffuse brightness,
not the underlying wall's pigment or dynamic spotlight illumination.
Decals now draw immediately after opaque compositing, **before** ordinary
native/GPU alpha-blended world geometry. Translucent glass, grates and similar
foreground surfaces therefore blend over decals instead of being painted over
by them. Hardware depth and the existing receiver-plane depth clip remain
active; opaque and alpha-tested receiver pixels behave as before. Very complex
transparency stacks are not depth-peeled, so physically exact sorting with
intersecting translucent receivers remains future work.

`renderer/sprite_decal_gpu_mesh.hpp` converts clipped, world-space
triangles to the same vertex/index/group format as the existing GPU 3DO
renderer, retaining the SPR's native MAT pointer, cel tracking and original
pixel-center UV convention. It builds bounds and surface normals; receiver
lighting is applied in the projected-decals shader with no new texture format. Its intended draw path must
preserve **per-Thing opacity** independently of ordinary 3DO face alpha.

**First GPU submission path (experimental, off by default):** set

```json
"enable_sprite_decals": true
```

in `jkgm.json` and load a type-`3` SPR placed near an **actual JK
world surface** (not a 3DO-only wall). Each sprite defines a **centered
box** with source SPR width and height and fixed 0.25 JK-unit depth.
The Thing **Up axis is the outward surface normal**: projection goes
along -Up, while Right/Look span the textured decal. This corresponds
to JK's native Thing-surface attachment convention.

The renderer caches native world polygons **once per world**, then
checks only their in-memory AABBs before clipping candidate receivers.
The result is a MAT-textured mesh drawn in the GPU alpha pass. Each
attached Thing caches its projected mesh until its attachment/size
changes or its position moves significantly; tiny physics jitter
does not repeatedly scan or rebuild the world. An unattached moving
Thing may still update its projection, so blood projectiles should
spawn a separate type-3 splat upon impact rather than project while
in flight. If a type-3 projectile becomes attached to a **world face**,
the renderer reads the same attachment normal exposed by
`GetThingAttachNormal()`, rotates the decal projector **into the
hit surface**, and centers its 0.25-unit projection depth around the
contact plane. The original Thing orientation is not modified.
The projector mesh is regenerated after attachment, rather than
created once at Thing creation. The renderer offsets clipped
triangles by 0.001 JK units and depth-tests against the scene.
GPU instance resources expire after they are no longer used.

If the source MAT is not yet resident, the projector is invalid, or
no acceptable level polygons lie in the projection volume, native
SPR rendering remains as a fallback; this is not proof that
projection was applied. Static Things with no valid receiver cache that
negative result until their pose/attachment changes, avoiding expensive
per-frame level scans. With `profile_renderer` enabled,
`SPR decal GPU stage=no_receiver` reports whether the scanned world
was valid and why candidate faces were rejected. The new
`SPR decal GPU stage=geometry` line reports actual submitted
projectors and triangle counts. A separate `SPR decal GPU stage=queued` log confirms the mesh actually
entered the GPU draw queue after its MAT became resident. The earlier
diagnostic-only `SPR decal probe` remains for comparison.

**Not yet verified in-game:** GPU submission, opacity and UV orientation
must still be compared to actual JK world walls. The current receiver
scanner supports **level architecture only**. Rigid 3DOs, animated
3DOs, and receiver sector restrictions are future work. This mode
does not need `use_model_cache` or `enable_mesh_collision`.

## Center-occluded SPR flares (experimental)

SPR type **`4`** is a camera-facing flare whose **center** must pass
the opaque-scene depth test; the rest of its billboard may overlap foreground
geometry. The native JK23 face markers and dedicated OpenGL transparency pass
are implemented. The shader reconstructs the SPR's projected center using
its UVs, samples resolved opaque depth, and applies small spatial edge
softening. It does not perform temporal fades yet.

The native JK23 addresses were rechecked against both supported patched
executables. The native converter inlines the render-list flush; the
original 0x43C590 hook was never reached, and its vertex addresses were
actually pointing at triangle data. The hook now tags D3DTLVERTEX.specular
at the std3D vertex-upload boundary instead. **In-game verification is
still needed.** Roll and per-Thing opacity remain independent, and
non-flare SPRs retain ordinary depth behavior.

**Troubleshooting type 4:** `SPR pose ... loaded_type=4` and
`SPR per-Thing alpha and flare markers enabled` establish only resource
parsing and Detour attachment, **not actual flare rendering**. With
`profile_renderer=true`, the DLL now emits up to six diagnostic messages
for each critical stage: `stage=draw_enter`, `stage=face_append`,
`stage=alpha_converter`, `stage=hw_upload`,
`stage=d3d_triangle` and `stage=gl_pass`. The first missing stage
isolates where the flare flag stops propagating. The counter is bounded
to avoid per-frame log spam.

A working flare looks identical to a normal sprite when fully visible or
when its center is behind a wall. To see the difference, position the
**center** just outside a building's silhouette so its edges still overlap
the building: a type-4 flare should remain whole while a default sprite
would be partially clipped.

## Per-Thing SPR animation (JK23, work in progress)

The experimental `SetSpriteInfo` / `GetSpriteInfo` COG verbs operate on **a
sprite Thing**, not the shared SPR resource:

```c
SetSpriteInfo(mySprite, VectorSet(1.5, 0.75, 0.5), 0.4);
vector info = GetSpriteInfo(mySprite);
```

An independent roll control is available for billboards, flares and smoke:

```c
SetSpriteRoll(mySprite, 90.0, 0.25); // animate 90 degrees over 0.25 s
float roll = GetSpriteRoll(mySprite);
```

Roll uses degrees around the sprite's own plane normal, so ordinary
camera-facing sprites rotate **in screen space**. It applies to extended
SPR orientations too. Rotation is independent of width, height and opacity;
a 360-degree transition makes one full revolution. It is per Thing, not per
shared SPR definition. The existing native preclip stage transforms the four
vertices in camera space without altering UVs or requiring an additional
SPR type flag.

The vector is `(width multiplier, height multiplier, opacity)`, and the
second argument is the linear transition duration in seconds (zero for
instant changes). `GetSpriteInfo` returns the current interpolated values.
Width and height start at 1, with 0 hiding that dimension. An opacity of 0
currently hides the sprite entirely.

**Status:** per-Thing width/height animation is wired into native SPR quad
generation, including ordinary and extended sprite types. It changes no
shared `.SPR` dimensions, materials, or UVs. The optional frustum hook
conservatively expands the visibility radius for enlarged instances.

**New, awaiting in-game verification:** fractional opacity now enters JK23's
native render-cache conversion through two signature-checked Detours.
Only proc-faces created during a SPR draw are tagged. Their alpha is set to
`round(original_face_alpha * instance_opacity)`, with originally opaque
faces using baseline 255 and originally translucent faces retaining the
native translucent baseline (normally 90). The renderer's existing
transparent D3D-state pipeline then processes the resulting vertices.
These changes are **per draw/Thing**: no shared `.SPR` or `.MAT`
data are modified. Fully transparent or zero-sized SPRs are skipped by
the draw hook.

This relies on JK23's validated proc-entry offsets and a fixed native
alpha-selection hook; if their signatures differ, intermediate opacity
is not available and an explicit warning is logged. CI covers alpha-tag
math and Windows compilation, but full transparency routing, animated
fades and interactions with other alpha materials still require in-game
tests.

The engine Thing signature prevents stale data from being applied to a
reused Thing slot. The registry is reset when SPR hooks are released.

## Experimental extended SPR type foundation (JK23 only)

The checked-in `tools/patch_jkgm_sprite_types.py` verifies the exact
development JK23 executable SHA-256 and changes *only* the `sithSprite_Load`
maximum type from 2 to 63. It refuses unknown EXEs and never overwrites the
input. Example (run on a copy of your executable):

```sh
python tools/patch_jkgm_sprite_types.py jkgm.exe jkgm-spr.exe
```

The experimental SPR draw hook honors these *authored* SPR type bits:
`0x04` preserves the Thing Up vector, `0x08` its Look vector,
`0x10` its Right vector; combining two preserves the complete plane.
`0x20` reserves decal projection and *currently only produces an oriented
flat quad*, **not yet a depth-projected decal**. For an upright billboard,
set the SPR file's decimal type to `4`. For fixed orientation use `12`
(`0x04 | 0x08`). Authored types 0, 1, 2 remain JK-native camera billboards.

**Important JK23 binary quirk, fixed in the sprite hook:** `rdSprite_NewEntry`
initially writes the file's type to `rdSprite+0x20`, but
`sithSprite_Load` later overwrites `+0x20` with the **World SPRITES
resource index**, not the authored type. Previous experimental builds
accidentally interpreted these resource indices as orientation flags,
causing trees/grass/rocks to behave inconsistently even when their SPR
definitions specified types 4 and 0. The new renderer captures the
author-specified type from the loader call and retains it in per-resource
metadata until free; it never modifies JK's index. The original MAT, UV,
and primitive clipping pipeline remains unchanged. Signature checks
disable these extensions on unknown executables.


The SPR loader patch is necessary before types higher than 2 can load.
The newer type-3 SPR decal projection system is documented above. Textured
particle rendering and other experimental sprite extensions remain separate.
The patch script ships with CI test builds; the repository distributes no EXE.
