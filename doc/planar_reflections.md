# Planar reflection foundation

**Status: groundwork only, no render-to-texture capture yet.**
`reflection_mode: "planar"` is recognized in compiled material-pack
`metadata.json`. Until a dedicated capture target is implemented, it falls
back to the same world-aligned environment image as `envmap` and `screen`
(when one is supplied); it does not produce actual scene reflections.

The existing modes retain numeric values 0–3; planar is 4. Probe and cube
modes are intentionally not defined yet.

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
