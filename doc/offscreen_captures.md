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
Captures no longer impose the former 96-unit far projection. Depth
range comes from the native camera and cached world bounds; dedicated
Hemi/Cube probes independently use a configurable Thing-only distance
budget. Capture 3DO UVs use the same 256-pixel-per-axis MAT reference
cap as regular cached 3DOs; world and sky UVs retain native scaling.
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

All realtime cube, shared, hemisphere and explicit COG cubemap captures
attempt independent, per-capture-sector portal visibility. Each 90-degree
face uses clipped portal windows for sky/world surfaces and an extended
sector mask for Thing discovery before pose acquisition. Dual-paraboloid
capture conservatively unions six ordinary 90-degree portal views to cover
the sphere instead of using an invalid wide-angle perspective window.
Unresolved source sectors, topology or portal clipping fail open to the
original full-world frustum path. The old nearest-64 offscreen-Thing count
limit was also removed: dedicated Hemi/Cube probes instead use a separate
configurable Thing range, not a nearest-N budget.

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

## Camera-to-MAT live video material

The implemented COG command is:

```c
CameraToMat(cameraThing, targetMat, FOV);
```

- `CameraToMat(cameraThing,targetMat,FOV)` registers a live material camera.
  The Thing's current position and orientation are read at every refresh.
  The native active JK camera remains unchanged. Horizontal FOV is measured
  in degrees (positive values below 179), and FOV=0 uses orthographic
  projection with an initial fixed one-JK-unit vertical view span.
- The target is captured at the original, resident MAT width and height and
  aspect ratio, never at main-renderer resolution. Nonresident MAT targets
  fail closed until their native MAT/D3D cache mapping is available.
- Each target has a dedicated retained RGBA16F offscreen texture and depth
  attachment, mipmapped after the render. Up to four MAT camera feeds may be
  registered; all active feeds refresh before the main GPU world pass.
- The generated texture **only replaces the base albedo image** (including
  existing SetMaterialAlbedo image transitions while active). Authored albedo
  factors, SetMaterialColor tint, UV scroll, emissive maps and factors,
  normal/surface maps, metalness, roughness, clearcoat and EnvMap behavior
  remain unchanged in both the GPU-cached and native-D3D rendering paths.
- Camera captures use cached world geometry, independently acquired native
  Thing poses, per-camera portal visibility (perspective), and the existing
  lightweight offscreen material shader. Hidden camera source Things remain
  valid if they have a live pose; destroyed/stale Things are rejected.
  Perspective captures include JK sky apertures. Orthographic captures use
  fog as background. As with current offscreen reflection captures,
  transparent/fluid/sprite effects and emissive-only geometry are not
  reproduced as a full main-camera postprocessing pipeline.
- All CameraToMat-generated images are suppressed while drawing *any* feed:
  capture sources use the original MAT albedos rather than recursively
  sampling live screens, avoiding same-target framebuffer feedback.
- `ReloadMaterial(targetMat)` or its alias `ResetMaterial(targetMat)` cancels
  the feed and restores authored/pack material defaults by clearing all
  per-MAT runtime COG overrides, including SetMaterialColor, SetMatEnvMap,
  SetMaterialAlbedo and SetMatClearcoat. Global material effects are not reset.
  Existing single-channel ResetMaterialAlbedo remains available separately.
- Camera requests and retained GPU targets are invalidated when changing
  worlds or renderer contexts; stale/deleted Thing signatures are rejected.

The initial implementation is testable through build and source contracts;
visual behavior, orientation and end-to-end capture parity still require
in-game testing. No new JSON option is required for this basic API.

## Dual-paraboloid capture culling and submission

For hemi probes, the six directional portal views are unioned once.
Independent offscreen Things are discovered and posed once per refresh,
then reused for both up/down disks. Native model radii reject Things
outside capture range before posing, and transformed AABBs conservatively
reject world faces and model groups fully outside a disk's 5-degree
overlap or far radius. Invalid bounds fail open.

Opaque and alpha-tested groups in each model node are batched by
material, cel, solid-color mode and face-cull state using MultiDraw.
Blended geometry remains excluded, and capture scheduling, ownership,
world shaders and resolution are unchanged. Profiles include
`scene_preparations`, `thing_range_culled`, `groups_bounds_culled`,
`world_bounds_culled` and `multi_draw_batches`.

## Cube/shared probe acquisition reuse

The six independent cube portal views are computed once per probe. Their
union supplies a single full-sphere native Thing scan and posed-3DO
acquisition shared by every face, using only conservative radial model
culling before posing. Each face still performs its own portal-sector,
frustum and material-group culling before drawing. A failed portal view
invalidates the union mask (fail open). This also applies to shared and
on-demand CaptureEnvMap cube captures without changing animation, owner
exclusion, capture cadence or resolution. A refreshed cube logs
`scene_preparations=1` rather than six.

## Hemi/Cube reflection capture CPU reuse

World-cache surfaces are organized into immutable material/cel/solid buckets
once per world and mesh revision, preserving the original sorted-key order
and the source order within each bucket. The buckets are reused across
capture faces, probe owners, and later frames without caching any camera-
specific visibility: portal, hemisphere and frustum tests still run for each
capture direction. Resetting or rebuilding the world cache invalidates them.

Each probe refresh also resolves each distinct native MAT/cel/solid key only
once for its two or six faces. This temporary material snapshot is not kept
across refreshes, so COG MAT cel changes and runtime material fades remain
live. Repeated consecutive shader/texture submissions for the same key are
skipped within one face (including world/model UV-mode separation); each new
face and sky pass starts with fresh material bindings. World MultiDraw arrays
and model-group scratch storage retain capacity across face renders.

The expensive GL state snapshot/restore is performed once around the complete
capture instead of once per face. Every face still explicitly establishes
its own framebuffer, viewport, clear, depth, cull and shader state. Separate
planar captures and global EnvMap publication keep their own state handling.

## Dedicated probe capture distance

`reflection_probe_max_distance` defaults to `64.0` JK world units and can
be set to `0` to disable the local-probe radial limit. Only dedicated
`hemi` and `cube` Thing probes use it. The limit rejects Things beyond
the native model bounding radius before independent pose acquisition
and skips fully out-of-range 3DO mesh groups at draw time. A large 3DO
whose bounds intersect the radius is retained conservatively; its
individual fragments are not clipped at the Thing budget distance.

All world surfaces, including sky apertures, retain the full scene-derived
projection depth and their normal portal/hemisphere/frustum visibility.
There is no sky-distance override because world geometry never receives
the dedicated local-probe limit. `shared`, `planar` and explicit
`CaptureEnvMap` likewise remain unrestricted by the Thing-distance
budget. Capture projections remain finite, as required by the GPU.
