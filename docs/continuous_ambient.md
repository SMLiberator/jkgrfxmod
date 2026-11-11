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

## Stage B0: GPU-ready sparse buffer packing

The CPU profiler established that a JkGR test world uses ~44k occupied
sector-local voxel nodes. A dense world-wide 3D texture would waste memory.
Stage B0 instead defines a GL 3.3-compatible, bounded sparse hash table
without changing the rendered scene.

- renderer/continuous_ambient_gpu.hpp exports two contiguous arrays:
  GL_RGBA32UI voxel keys (sector, signed XYZ grid coordinates as uint32
  bits) and GL_RG32F values (authored brightness, shared live-lit value
  or -1 for an ordinary, single-sector node).
- Each key is inserted deterministically in sorted order, with at most
  32 linear probes. If that bound cannot be met, the table grows rather
  than introducing unbounded lookup latency.
- assets/shaders/continuous_ambient_lookup.glsl describes the matching
  GLSL 3.30 usamplerBuffer/samplerBuffer lookup. It is NOT YET included
  by the production GPU 3DO shader; texture units remain untouched.
- The owner-sector low-frequency fallback stays separate. GPU vertex
  sampling will interpolate between eight world-aligned grid corners
  and use the fallback only when a sparse key is absent.
- update_shared_floors() recomputes shared-node values from all connected
  owners on the CPU, preserving continuity during scripted ExtraLight
  changes. It fails atomically if any live floor is invalid.
- The key/value pair requires 24 bytes per hash slot, plus optional
  CPU-side shared ownership lists. Both memory and maximum lookup
  probes are measurable before GPU shader integration.
- Windows tests verify negative grid coordinates, identical positions
  in disconnected sectors, deterministic packing, live portal floors,
  failed floor reads, and a synthetic 40k+ voxel workload.

There is no new rendering option or visual change in Stage B0.
Stage B1 will allocate/upload texture buffers and sample them per
3DO vertex behind a separate default-off GPU experiment.

## Stage B1: isolated GPU 3DO experiment (default off)

Set `"experimental_continuous_ambient_gpu": true` to run the first
per-vertex GLSL 3.30 sparse-voxel lighting experiment. The original
`experimental_volumetric_ambient` 2x2x2 model remains available for
comparison and is suppressed ONLY while the new GPU field is valid for
the frame. The CPU profiling option remains independent.

The renderer uploads two texture buffers per SithWorld revision:
RGBA32UI sector+world-voxel keys and RG32F authored/shared-live values.
The vertex shader converts the existing camera-space vertex back to
world space, retrieves eight voxels by bounded hash lookup and performs
trilinear interpolation. Missing sparse nodes use each sector's
analytically evaluated 2x2x2 fallback at the same world-aligned grid
position. All nodes near connected portals use reconciled static
samples; shared nodes use live ExtraLight averaged across their
participating sectors. The old native 3DO Gouraud and 0x20 exemption
paths remain unchanged.

The two new buffer samplers use dedicated texture units 21 and 22
(sector crossing 19, sky aperture 20). Vertex shader compilation,
runtime allocation and draw submission all require >=23 fragment and
combined units plus at least two vertex texture units. Low-end GL 3.3
devices automatically retain their previous lighting. Sector-crossing
lighting and the new sparse GPU ambient remain mutually exclusive for
this first performance experiment.

GL textures/buffers are released on world or mode changes; native
sector ExtraLight is read once per sector per frame, and the dynamic
values buffer is reuploaded only when sector floors change. Failed
reads or GL updates leave the legacy renderer available. This first
GPU variant is intentionally 3DO-only: cached world geometry retains
authored baked lighting, and sprite/decal/HUD/sky models are excluded.

The user-visible differences now require an in-game A/B test across
connected sector boundaries. Prior Stage A CPU coverage at model
origins does NOT establish visual continuity on all mesh vertices or
measure GPU runtime costs.
