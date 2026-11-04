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
two graph edges: owner -> adjacent -> adjacent. Each neighbor must pass
a conservative Thing sphere/sector AABB overlap check before the search
can continue through it. It never scans all world sectors per Thing.
A per-world hash lookup resolves the native sector ID to an array index,
so sparse IDs are handled directly.

The ordinary candidate budget is **four sectors total**, including the
owner. Exactly four is valid; a fifth overlapping connected candidate
or an invalid linked list causes a safe fallback to JK's existing
single-sector behavior. The BFS queue is a fixed four-element array,
requiring no per-Thing allocations. Ordinary single-sector geometry
takes the existing shader path unchanged. All engine reads are read-only.

This stage deliberately does not change gameplay or shaders yet.
The isolated C++ tests cover two adjoining rooms, 1 -> 8 -> 3 adjoin
hops, disconnected but spatially overlapping sectors, sparse sector
IDs, slanted planes, interior collision boxes, invalid/cyclic native
adjoin links, exactly four candidates and candidate overflow.

## Stage 2: instance staging

Store the volume snapshot for the current SithWorld and invalidate it on
world changes or geometry/membership changes. For each GPU-cached Thing,
transform its conservative mesh bounds with the *actual* affine R/L/U
matrix, including scaling or mirroring. Use the bounded adjoin graph
query once per instance, not a full-sector scan or per-pixel raycast.

Keep the existing fast vertex-lighting path whenever only the owner
sector is relevant. Send a small bounded plane/lighting set to the shader
only for objects that intersect multiple sectors.

## Stage 3: per-fragment lighting and tint

Interpolate the fragment world position and classify it against the
candidate sector planes, preferring the owning sector on coincident
boundaries and retaining native ownership when no candidate contains it.
Fetch live per-sector ambient/extra, sector tint and CMP tint, without
changing SithThing.sector, collisions, COG scripts or visibility.

The current GPU 3DO shader computes baked/gamma-corrected JK vertex
lighting and global color effects before interpolation. Keep that
behavior unchanged for objects wholly inside one sector. The crossing
path needs a separate per-fragment reconstruction of static lighting
and tint from existing raw vertex attributes; avoid simply multiplying
the interpolated color by another sector's brightness or applying the
camera's sector tint twice. Preserve authored emissive maps, fullbright
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
with the custom JKGM executable, validate it before activating the
shader stage.
