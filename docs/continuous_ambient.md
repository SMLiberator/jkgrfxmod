# Continuous volumetric ambient: CPU topology prototype (Stage A)

**Status:** CPU-only. The current experimental_volumetric_ambient
2x2x2 GPU path, all existing JSON options, renderer behavior and shader
requirements are **unchanged**. This commit produces no visual difference.
Stage B GPU integration requires separate review and tests.

## Target

A world-aligned, sector-aware sparse voxel lattice. Separate sectors
may contain voxels at identical XYZ coordinates. Nodes at validated
passable Adjoin polygon openings are reconciled to common values, so
trilinear sampling through those openings does not depend on a Thing's
owning sector. Unconnected sectors keep independent values even where
their world-space coordinates coincide.

The CPU field is in renderer/continuous_ambient.hpp.
renderer/continuous_ambient_capture.hpp adapts existing native world
static Gouraud capture and the validated passable-portal polygon reader
from renderer/world_vertex_ambient.hpp.

## Current algorithm

- Fixed global voxel spacing 0.25..4.0 (default 1.0 JK unit).
- Splat each authored vertex into 27 nearby sparse nodes, keyed by
  (sector ID, global integer voxel coordinate). No dense world-sized
  3D texture. Quantize final node values to uint8 in [0,1] or [0,2].
- Capture finite, non-sky, passable Adjoin triangles. Near their actual
  polygons rather than their infinite planes, connect the sector IDs
  at common grid nodes. Connected components handle three-way portals.
- Compute shared voxel brightness from an immutable snapshot of the
  original authored samples. Portal order cannot progressively contaminate
  later portal values. Bound native portal size and total sparse node count.
- Trilinearly interpolate eight neighboring values. If nodes are missing,
  renormalize surviving weights; if none contribute, report no sample.
- Preserve live sector ExtraLight separately from static quantization.
  A shared node averages individually floored sector values at sample
  time; SetSectorLight fades therefore require no static rebuild.
- Reject nonfinite coordinates, invalid native surface data, unsupported
  light modes and out-of-range memory layouts.

## Tests

CI compiles tests/continuous_ambient.cpp against the real native capture
adapter and runs tests/continuous_ambient.py. Covered scenarios include
a non-grid-aligned doorway, disconnected rooms separated by a wall,
three connected sectors, live sector ExtraLight fades, HDR caps,
invalid coordinates, and capture of invisible passable Adjoins that do
not appear in the rendered mesh. Solid and sky Adjoins are excluded.

## Remaining work

1. Extend coverage to empty interior cells far from authored vertices:
   current 27-node splatting is intentionally sparse and can leave gaps
   in very large rooms.
2. Evaluate oblique/large portals and walls close to openings. The
   conservative sqrt(3)*spacing stitching band can soften light within
   approximately a voxel of a doorway; it is not physical light transport.
3. Profile CPU build time and peak memory on an actual JkGR world.
4. Add a compact GPU representation behind a separate default-off mode,
   retaining the current sector 2x2x2 approach for A/B comparisons.
5. Decide how dynamically modified native Adjoin connectivity refreshes.

No OpenGL version increase is needed for this CPU stage. OpenGL 3.3
texture buffers remain a viable GPU transport; OpenGL 4.3 compute
shaders and shader-storage buffers could be an optional later upgrade.
