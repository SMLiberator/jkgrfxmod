# Planar reflection foundation

**Status: independent reflected-scene render-to-texture debug capture available (opt-in).**
`reflection_mode: "planar"` is recognized in compiled material-pack
`metadata.json`. The new capture renders real cached-world geometry and
independently acquired animated 3DOs into a separate color/depth target.
**The main material shaders do not sample this target yet**: until connected,
`planar` still falls back to the existing environment mapping when supplied.

The existing modes retain numeric values 0–3; planar is 4. Probe and cube
modes are intentionally not defined yet.

## Independent Thing discovery (implemented)

OpenJKDF2's `sithRender_RenderThings` walks the Thing lists of the
sectors chosen by the camera. The new diagnostic scans **every**
SithSector and follows each native `pNextThingInSector`, without
calling JK's render traversal or assuming a `SithThing` allocation stride.

- `SithWorld`: sector count at `+0x4c`, sector array at `+0x50`,
  loaded Thing capacity at `+0xbc`, Thing table at `+0xc4`.
- `SithSector`: stride `0x98`, first Thing at `+0x30`.
- `SithThing`: owner sector `+0x5c`, next Thing `+0x60`,
  signature `+0x74`, position `+0xb8`, rdThing at `+0xc4`.
- `rdThing`: render type at `+0x00`, rdModel3 pointer at `+0x04`.

Every read is bounded, cycles/duplicate nodes cause failure, and malformed
lists fail closed. No writes to native state occur. Flag `0x2` (destroyed),
`0x8` (level geometry), `0x10`, and `0x80000` (disabled) are excluded;
camera-type Things are excluded from the model candidate list.

Set `experimental_planar_thing_discovery: true` to log diagnostic counts
approximately every 120 frames. It **does NOT render** reflections.

The experimental diagnostic now also matches all-sector Things against the
actual GPU 3DO node snapshot from the main camera, using Thing pointer plus
signature (including with shadows and decals disabled). Matched Things may
have multiple cached mesh nodes. The unmatched count is **not** strictly
an offscreen count: native fallbacks and unloaded MATs can also be missing.
`renderer/planar_reflection_workset.hpp` can prepare reflected model-view
matrices for the matched nodes without touching JK's camera. The separate
native-node path below handles Things not submitted for the main camera.

## Independent native 3DO pose and GPU mesh acquisition (experimental)

`renderer/planar_reflection_native_nodes.hpp` validates the tested binary's
rdModel3 node accessor, the Thing signature/model owner, the active geoset,
the entire parent hierarchy, amputations and the current contiguous joint
pose. The native rdModel3_GetMeshMatrix accessor refreshes a Thing's
animation cache once; no native camera or draw callback is changed.
Malformed parent cycles and stale poses fail closed without partial output.

`renderer/main.cpp` now exposes `acquire_planar_reflection_offscreen_gpu`,
which can snapshot previously unseen native rdMeshes using the same shared
GPU geometry cache and material-readiness/warmup mechanisms as ordinary
GPU 3DOs. This generates `gpu_3do_instance` entries with independent
world-space joint transforms. The code remains on-demand and separate from
the gameplay frame, shadows and native renderer. Initial capture lighting
uses cheap baked/ambient values and no dynamic lights.

When `experimental_planar_thing_discovery` is enabled, approximately every
120 frames a rotating batch of at most 16 Things (including those partially submitted
by the main camera) is probed independently, and
`PLANAR discovery` logs pose, mesh snapshot, texture-readiness and node
counts. These nodes are **not yet drawn**. Missing meshes, invalid poses or
unready materials must not produce a partial reflection. Default is off.

Independent GPU-ready mesh acquisition now feeds the debug capture.
Animated Thing hierarchy completeness is checked before admission. The
independent sector scan honors native ThingFlags `0x10` and `0x80000`, plus
`SetThingCurGeoMode` values 0–2 (no filled hardware geometry), and the GPU
acquisition rechecks both states immediately before pose refresh. This is
independent of the primary camera's frustum/portal visibility; models outside
its view must remain available to a mirror. GeoMode 3 is respected as solid
color; 4 is fully textured. The runtime visuals still need in-game verification.

## Experimental capture and debug view

In `jkgm.json`:

```json
"experimental_planar_reflection_capture": true,
"planar_reflection_resolution_scale": 0.5,
"planar_reflection_debug_view": true
```

The scale is validated to `[0.125, 1.0]`, defaults to `0.5`, and applies to
the game's internal screen resolution. At 1920x1080, the default target is
960x540. Debug view **replaces the displayed scene** after postprocessing
and HUD, showing the raw framebuffer without modifying gameplay rendering.
Turn `planar_reflection_debug_view` off to keep the normal picture.
`experimental_planar_reflection_capture` is disabled by default.

The first version chooses one planar-material world surface, preferring
the nearest plane; without an explicit planar MAT it can choose a world Fluid
surface for water diagnostics. A passable-adjoin surface is an eligible
reflection RECEIVER when its material is planar or Fluid, even though such
portal geometry is excluded from the mirrored SCENE render. This matters for
water boundaries authored as traversable sector adjoins. The selector logs
`planar_matches`, `fluid_matches`, and `passable_reflection_receivers` to
diagnose whether eligible planes were found. No arbitrary fallback plane is
fabricated.
It uses all cached world-sector geometry rather than the main camera's portal
candidate list, draws the animated 3DO nodes from up to 128 nearest native
Thing owners (with primary GPU nodes as fallback), and excludes sky, HUD,
projected decals, dynamic lighting, transparent Fluid blending, shadows,
GGX, SSAO and postprocessing. It restores GL framebuffer/texture/render
state after the pass. Captures are full frame, not yet cropped to the planar
surface. It logs the dimensions, geometry counts and capture **CPU** time
periodically; there is no independent GPU timestamp yet.

**Limitations:** This is a raw diagnostic preview, not finished water or mirror
materials. The renderer still needs planar texture sampling/projective UVs,
correct reflected sky, alpha/VFX parity, independent reflection frustum and
scissor pruning, multi-plane capture sharing and GPU-timing feedback.

## Capture contract

- A source material selects planar reflections. Material effects (roughness,
  specular strength, normal maps, fluid absorption, etc.) remain independent.
- Surfaces that share an equivalent plane may reuse one reflected image.
  A visible plane must be selected before any capture work.
- A secondary `engine_camera` is reflected using the plane normal and
  offset `dot(normal, world_position)==offset`.
- `renderer/planar_reflection_camera.hpp` provides GL-free, tested world-to-view
  matrix math matching the GPU-cached world and 3DO matrix layouts. Captures
  must not modify JK's live rdCamera, and must not trust a mirrored camera's
  SithSector to exist.
- The reflection pass **must include cached 3DO models** from the existing
  frame snapshot as well as GPU-cached world and sky. It must frustum-cull them
  independently of the main camera's portal/sector visibility; otherwise
  objects visible only to the reflection will disappear.
- The mirrored basis reverses handedness. Restore front-face winding/culling
  state after capture. Clip geometry against the water/mirror plane.
- Render to a separate low-resolution color/depth target with simple lighting;
  exclude nested reflections, unnecessary postprocessing, shadow-map
  generation, expensive alpha effects, camera-HUD and decal passes.
- Never sample the texture being rendered into. Keep the normal frame state,
  texture-unit ownership and cached material data intact.
- The initial usable version should work on horizontal water and vertical
  flat mirrors. Models are required before it is declared feature-complete.
- Only after correctness is verified should we add distance culling, 3DO LOD
  bias, projected-water-area scissoring, periodic updates or multi-plane
  scheduling. Record capture CPU and GPU timings independently.

## Testing

`tests/planar_reflection_camera.cpp` validates reflection geometry and 3DO
native model-view compatibility, including nonunit and oblique plane normals.
`tests/planar_reflection_camera.py` verifies mode registration and shader
fallbacks; `tests/planar_reflection_render_math.cpp` verifies resolution and
signed clip-plane computations; `tests/planar_reflection_capture.py` checks
native offscreen/FBO/GL-state/shader/config integration. Automated tests do
**not** establish correct live reflection visuals; inspect the debug view
in Wine/Windows before connecting it to MATs.
