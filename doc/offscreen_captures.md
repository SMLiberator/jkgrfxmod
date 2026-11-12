# Offscreen capture framework

The renderer now has independent, reusable **GPU-only** capture primitives:

- `renderer/offscreen_capture_target.hpp`: an RGBA16F color texture with a
  depth attachment, lazy allocation/reallocation and scoped GL state restoration.
  One instance is currently owned by the planar reflection pass; this class
  does not require a water plane and can support other cameras.
- `renderer/offscreen_capture_projection.hpp`: validates dimensions and
  computes perspective/orthographic projection for an independent camera.
  FOV is the full **horizontal** field of view in degrees. FOV `0` selects
  orthographic projection and requires a positive vertical span in world
  units; target aspect derives from the output dimensions.
- `assets/shaders/planar_capture.vert/.frag`: lightweight world/3DO capture
  shader. The new orthographic and optional clip-plane paths are dormant
  for planar reflections; existing capture uses the same perspective
  parameters and enabled water-plane clipping as before.

## Camera-to-MAT design — not yet an exposed COG verb

The intended user-facing command is:

```c
CameraToMat(cameraThing, targetMat, FOV);
```

- The validated, actual dimensions of `targetMat` determine target resolution
  and aspect ratio. Do **not** silently use the main screen resolution or
  upscale/downscale the requested MAT target.
- The camera Thing controls position and orientation; the command must never
  move or replace JK's active rdCamera. `FOV=0` is orthographic, with a
  separate world-space vertical-span control (e.g. future
  `SetCameraToMatOrthoHeight(targetMat, height)`).
- Target MAT must resolve to a valid GPU-cached material/texture binding,
  with safe lifetime across MAT reloads, level changes and static MAT IDs.
  Arbitrary OpenGL textures cannot be passed to existing runtime MAT image
  overrides until a **render-target** image source is introduced.
- Capture scheduling, optional color grading and analog/noise effects should
  use separate verbs/settings; the basic command remains three arguments.
  Initially cap capture count and prefer explicit update intervals.
- Avoid feedback loops: while drawing a camera target, never sample that same
  active color attachment. Suppress or substitute recursive targets, or later
  double-buffer the previous frame.
- World faces eligible for the secondary camera require live light/UV/CMP
  data independent of the primary portal traversal, and offscreen-only 3DOs
  must honor ThingFlags/CurGeoMode. Main-camera visibility is not a substitute.

**Current state:** camera-to-MAT is a documented API contract, **not** registered
as a callable COG verb, and no live MAT binding exists yet. The next phase
needs a capture request registry, arbitrary camera Thing validation,
render-target MAT texture routing and scheduling. Keep planar reflection
parity/CI as a regression baseline during that work.
