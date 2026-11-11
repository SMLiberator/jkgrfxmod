# Continuous volumetric ambient: CPU topology prototype (Stage A)

**Status:** CPU-only. The current experimental_volumetric_ambient
2x2x2 GPU path, all existing JSON options, renderer behavior and shader
requirements are **unchanged**. This stage produces no visual difference.
Stage B GPU integration requires separate review and tests.

To profile the CPU-only field on a real JkGR world, set
`"experimental_continuous_ambient_cpu": true` in jkgm.json. Leave
`"experimental_volumetric_ambient"` at whichever value you are already
comparing; the new option does **not** change lighting. A one-time
`CONTINUOUS AMBIENT CPU snapshot` log contains capture/build microseconds,
raw/final node counts, represented sectors, and portal-stitching counts.
With `"profile_renderer": true`, a periodic
`CONTINUOUS AMBIENT CPU coverage` log samples owning SithThing origins
with zero extra-light floors to estimate missing static voxels. The
CPU field is retained only while the option is enabled, and its cached
snapshot is reset on world changes.

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
- Trilinearly interpolate eight neighboring values. Missing sparse nodes
  use an analytic, sector-wide eight-corner low-frequency fallback
  derived from the original authored vertices, with the same world-aligned
  sample coordinates. This fills even large empty interiors without a
  giant dense allocation. A sector with no authored data and no connected
  shared nodes still reports no sample.
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

1. Compare the current sparse+fallback field against the old 2x2x2
   field in unusually large rooms to see whether the low-frequency
   fallback sufficiently preserves broad baked gradients.
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

## Coverage prototype follow-up

The first CPU version had empty sample regions whenever a sector was
wider than the splat radius of nearby authored vertices. The updated
prototype stores a small 2x2x2 fallthrough field per represented sector,
computed from the SAME authored source vertices during finalization.
A missing world-aligned node uses that low-frequency value at the node's
world position. Interpolation remains continuous across cell boundaries,
while valid authored and shared portal nodes retain priority.

This is intentionally **not** a dense voxel fill. A giant empty sector
can remain sparse in memory. It does not yet guarantee an ideal spatial
light distribution in extremely large or complicated sectors; that
remains a visual quality and profiling question for the GPU prototype.
