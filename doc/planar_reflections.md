# Planar reflection foundation

**Status: groundwork only, no render-to-texture capture yet.**
`reflection_mode: "planar"` is recognized in compiled material-pack
`metadata.json`. Until a dedicated capture target is implemented, it falls
back to the same world-aligned environment image as `envmap` and `screen`
(when one is supplied); it does not produce actual scene reflections.

The existing modes retain numeric values 0–3; planar is 4. Probe and cube
modes are intentionally not defined yet.

## Independent Thing discovery (implemented; capture still pending)

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
matrices for the matched nodes without touching JK's camera. This is still
inventory only; it does not draw, and cannot invent the missing Things'
animated joint poses.

This proves the independent discovery method but not yet GPU-ready mesh
acquisition for Things the normal view didn't submit. That will require
offscreen rdModel3/geoset and joint-pose handling without modifying JK's
live camera or submitting normal gameplay geometry.

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
fallbacks. These tests do **not** validate live reflection captures.
