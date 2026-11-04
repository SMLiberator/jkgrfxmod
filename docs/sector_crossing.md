# Sector-crossing lighting: staged design

## Current behavior

GPU-cached 3DOs inherit the ambient light, sector tint and CMP tint of
the owning SithThing's sector. A mesh spanning a portal remains uniformly
associated with that sector even when half its visible fragments lie in
another room. This is separate from dynamic point lights.

## Stage 1: CPU geometry foundation (this commit)

The standalone header renderer/sector_crossing.hpp reads native sector
bounding boxes and collision/portal boundary planes once per level. It
uses the containment semantics studied in OpenJKDF2's
sithIntersect_IsSphereInSector: collision faces (surface flags 0x4) and
movement-enabled adjoins (flags 0x2) define the sector halfspaces.
An optional interior collision box can shortcut the plane checks.
Offsets are those already used and validated by JKGM, not OpenJKDF2's
executable addresses.

The static snapshot now also extracts each SithSector's linked adjoin
list, using SithSurfaceAdjoin's destination-sector (+0x04) and next-link
(+0x10) pointers. These are native JKGM structure offsets, not the
OpenJKDF2 executable's absolute addresses. Adjoin topology is used even
if movement through an adjoin is disabled: a model can geometrically
straddle the same sector boundary.

Candidate selection now starts at the owning sector and follows at most
two graph edges: owner -> adjacent -> adjacent. The fixed 64-entry
traversal queue follows graph links even through non-overlapping intermediate
sectors, because each individual animated 3DO *mesh* can sit completely
outside its Thing's owning sector. The conservative transformed mesh
sphere/sector AABB test filters the final shader candidates rather
than the traversal links. It never scans all world sectors per Thing.
A per-world hash lookup resolves the native sector ID to an array index,
so sparse IDs are handled directly.

The ordinary candidate budget is **four sectors total**, including the
owner. Exactly four is valid; a fifth overlapping connected candidate
or an invalid linked list causes a safe fallback to JK's existing
single-sector behavior. The candidate list has four entries, while its bounded traversal queue
has 64 entries. Both are fixed-size and require no per-Thing allocations. Ordinary single-sector geometry
takes the existing shader path unchanged. All engine reads are read-only.

This stage deliberately does not change gameplay or shaders yet.
The isolated C++ tests cover two adjoining rooms, 1 -> 8 -> 3 adjoin
hops, disconnected but spatially overlapping sectors, sparse sector
IDs, slanted planes, interior collision boxes, invalid/cyclic native
adjoin links, exactly four candidates and candidate overflow.

## Stage 2 + 3: experimental shader implementation

The shader integration is implemented but OFF by default. Opt in with:

```json
{
  "enable_sector_crossing_lighting": true
}
```

The switch affects only GPU-cached native 3DO model Things, not sprites,
procedural geometry, decals, shadow casters or Camera HUD Things.
All normal single-sector models keep the existing vertex-lighting output.
There is no new COG verb and no mutation of the Thing's owning sector.

The renderer snapshots sector planes once per level into a GL 3.3 RGBA32F
texture buffer (texture unit 12). The same native six-field world signature
used by the cache invalidates this snapshot on level changes. For each
model, the GPU cache's existing group bounds are conservatively transformed
with the native R/L/U affine matrix; the Adjoin graph selects at most four
candidate sectors within two hops, even when an individual model mesh
lies entirely in the second room. Too many sectors, corrupted native
memory, a sector requiring more than 96 planes, or a missing owner sector
leaves the original native-owner lighting unchanged.

Live per-sector ambient, extra, CMP tint and SithSector tint are sampled
per crossing instance, not cached as static level light. The camera's
captured per-Thing ambient is the authoritative owner value; other
sectors use the *difference* between sector ambient + extra totals, so
existing global extra lighting and Thing overrides remain represented.
The shader independently evaluates JK gamma/color correction at vertices
for each candidate, then selects the already-interpolated candidate RGB
by fragment world position. This avoids gamma-after-interpolation
artifacts. Both vertex Gouraud and GPU per-pixel dynamic point lighting
modes are supported. The 8-bit CMP fullbright override is applied after
sector selection, and authored emission, sun/spotlight contribution,
transparency and existing postFX pipeline remain independent.

This is an experimental implementation, not yet gameplay-verified.
Special IR/override lighting, dynamic topology edits and models spanning
more than four sectors still need manual testing. See the CI source
wiring checks and the pure C++ native-memory staging tests.

## Implementation details and later improvements


Store the volume snapshot for the current SithWorld and invalidate it on
world changes or geometry/membership changes. For each GPU-cached Thing,
transform its conservative mesh bounds with the *actual* affine R/L/U
matrix, including scaling or mirroring. Use the bounded adjoin graph
query once per instance, not a full-sector scan or per-pixel raycast.

Keep the existing fast vertex-lighting path whenever only the owner
sector is relevant. Send a small bounded plane/lighting set to the shader
only for objects that intersect multiple sectors.

### Per-fragment sector selection

Interpolate the fragment world position and classify it against the
candidate sector planes, preferring the owning sector on coincident
boundaries and retaining native ownership when no candidate contains it.
Fetch live per-sector ambient/extra, sector tint and CMP tint, without
changing SithThing.sector, collisions, COG scripts or visibility.

The current GPU 3DO shader computes baked/gamma-corrected JK vertex
lighting and global color effects before interpolation. Keep that
behavior unchanged for objects wholly inside one sector. The crossing
path uses separate per-candidate vertex lighting and interpolates the
result before fragment selection. Do not simply multiply already-lit
pixels by another sector's brightness or apply the camera's sector tint
twice. Preserve authored emissive maps, fullbright
palette texels, transparency and RGB vertex colors.

Performance target: no extra GPU texture lookups/draw calls for ordinary
objects. No per-fragment raycast. No CPU triangulation or geometry
reallocation. Prefer conservative fallback to wrong sector membership. The four
candidate slots save per-instance shader uniform storage versus eight,
and can halve the worst-case per-fragment sector loop once GPU-enabled.
The adjoin query also removes the previous O(total world sectors)
candidate scan, replacing it with a graph neighborhood walk.

## Later in-game checks

Cars and long meshes spanning doorways; angled and concave regions;
overlapping sectors, dynamic adjoins, camera transitions, animated
materials, transparencies, 0x20 exemptions, scaled RLU transforms;
and more than four candidate sectors. If a native layout disagrees
with the custom JKGM executable, validate it before enabling the
experimental shader path.
