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

## Global world capture to a material EnvMap

The implemented `CaptureEnvMap(sourceThing, fadeTime);` COG command takes a
validated Thing's world-space position and queues one complete six-direction
world capture. The new image is not visible until all six 90° perspectives
and a GPU equirectangular conversion have succeeded. COG callbacks do not
touch OpenGL or switch the active JK camera.

- `envmap_capture_face_resolution` (default `128` in `jkgm.json`) sets the
  **fixed per-face size**, independent of internal renderer resolution.
  The output is `4*N × 2*N`, so six 128×128 views yield a 512×256 image.
- The converter renders into HDR RGBA16F and samples the cubefaces with a
  small angular footprint to smooth face edges. Its output wraps at the
  longitude seam; mipmaps are generated only after the complete conversion.
- Visible JK sky-flagged apertures (0x200 horizon / 0x400 ceiling) now use
  the **secondary camera's** native-style sky UV projection at far depth.
  Real world/model geometry correctly occludes them. Only truly uncaptured
  background, invalid sky settings and unsupported surfaces fall back to
  the scene fog color, avoiding black transparent gaps.
- A prior completed global capture remains visible while the new capture is
  built. `fadeTime` is measured from **successful publication** in seconds;
  `0` switches immediately. The previous texture is kept until the fade
  completes. Both world and GPU 3DO materials use the existing dual EnvMap
  samplers for interpolation.
- The new image only replaces the **global EnvMap**, not explicit MAT maps
  or per-MAT COG overrides. Material reflection strength, roughness, Fresnel
  and metalness remain unchanged. A later `SetGlobalEnvMap` or
  `ResetGlobalEnvMap` cancels this generated global override.
- Captures are on-demand, not updated each frame. Each request records a
  native Thing identity only for self-exclusion: an immutable position is
  copied at request time. World changes cancel obsolete requests.
- Global captures include world geometry and visible sky apertures, plus
  validated Cog (type 7) MODEL3 scenery, including 0x8 LEVELGEO Things and
  PATH (move type 2) Things at their current poses. Ghosts and all other
  Thing types are excluded. PHYSICS movement, Thing-to-Thing attachments,
  destroyed/hidden/disabled and camera HUD Things are excluded. Native
  ownership/signature checks apply to offscreen discovery and main-camera
  fallback equally. Assigned puppet resources and the sun-shadow dynamic
  caster hint no longer suppress legitimate one-time Cog captures.
  The realtime hemi/cube passes retain their own dynamic geometry policy.
- Other scenes keep their ordinary `hemi`/`cube` realtime per-Thing probes.
  This global map can also serve as their authored-EnvMap fallback when those
  materials permit the global override.

## Shared realtime reflection capture controls

`enable_reflection_capture=true` allows realtime `shared`, `cube`, `hemi`
and `planar`. If false, all four use EnvMaps, while explicit COG
`CaptureEnvMap` remains independent. `enable_reflection_probes=false`
disables individual cube/hemi and planar captures and routes these modes to
the camera-centered `shared` projection, even when no material explicitly
selects `shared`. The existing `enable_reflections` remains the master switch.


`"reflection_mode":"shared"` is a lazy, camera-centered six-face cubemap
(mode 7) for GPU-cached world and 3DO materials. When no visible material
uses this mode or falls back from a specialized mode, no shared probe is captured. The probe is refreshed using
`reflection_capture_update_interval` and `reflection_capture_resolution`;
it is independent of `reflection_probe_max_owners` (the local Thing budget).
It includes animated 3DOs and visible sky through the ordinary realtime
capture policy, not the Cog-only filter used by `CaptureEnvMap`. In
uncovered directions the shader retains the author's/global EnvMap fallback.
On GPUs without enough fragment sampler units the shared capture is
unavailable and EnvMap fallback remains.
The former `"reflection_mode":"matcap"` is no longer accepted; use a
separate `matcap_map` artist layer when that layer's shader support lands.


`reflection_capture_resolution` is the fixed square per-cube-face
resolution (default 128), and the short edge of the planar target.
Realtime and COG captures no longer impose the former 96-unit capture far
limit or hemisphere 3DO distance budget. Depth range is derived from the
native camera range and the cached level bounds. Sky apertures bypass CPU
far-plane rejection. Capture 3DO UVs use the same 256-pixel-per-axis MAT
reference cap as regular cached 3DOs; world and sky UVs retain their own
native scaling.
The `hemi` probe now stores **two fixed +Z/-Z dual-paraboloid disks**,
each twice the cube-face linear resolution with 5-degree horizon overlap.
It captures **two nonlinear paraboloid views directly**: the geometry shader
subdivides triangles, clips each hemisphere, and writes radial depth;
the sky pass reconstructs world-space directions. The old six-face cube
conversion reference has been removed.
Planar targets preserve the main camera's aspect ratio.

`reflection_capture_update_interval` (seconds, default 0) throttles
complete per-Thing cube and hemi captures and visible planar captures.
The last complete image and its original view transform remain bound until
a refresh, avoiding texture jitter from mismatched camera matrices.
The separate COG `CaptureEnvMap` command is still strictly on-demand and
uses its own fixed `envmap_capture_face_resolution`.

Realtime `hemi` and `cube` probes exclude their reflecting Thing and all
Things connected through qualifying Thing-to-Thing/Thing-face attachments:
the CHILD attachment must include `NOMOVE` (0x8) or JKGM's `NODE` (0x80).
Links are traversed bidirectionally across the connected assembly; ordinary
parent references, surface attachments and ordinary movable Thing attachments
do not join the exclusion family. Unrelated probes still reflect these parts.
The validated exclusion set is shared across every capture view in a refresh,
with bounded traversal for malformed native child lists. This is distinct
from the **ThingFlag** `LEVELGEO` (also 0x8), which remains fully renderable,
and does not alter the global `CaptureEnvMap` policy.

Visible JK 0x200/0x400 sky world faces are now explicitly rendered in all
secondary cameras with far-depth and native-style horizon/ceiling mapping.
These are apertures of the surrounding world, not ordinary UV faces; depth
from buildings occludes them as it does in the main camera.

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
