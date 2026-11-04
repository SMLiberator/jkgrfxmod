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

Bounding-sphere/AABB candidate selection narrows a Thing's potential
sectors to at most eight, with owning-sector-first ties and safe fallback
on candidate overflow or invalid geometry. Native IDs are not assumed
to equal sector-array indices. All engine memory is read-only.

This stage deliberately does not change gameplay or shaders yet.
The isolated C++ tests cover two adjoining rooms, sparse sector IDs,
angled planes, interior collision boxes, invalid pointers and overflow.

## Stage 2: instance staging

Store the volume snapshot for the current SithWorld and invalidate it on
world changes or geometry/membership changes. For each GPU-cached Thing,
transform its conservative mesh bounds with the *actual* affine R/L/U
matrix, including scaling or mirroring. Choose candidate sectors once
per instance, NOT per pixel or across the entire level.

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
reallocation. Prefer conservative fallback to wrong sector membership.

## Later in-game checks

Cars and long meshes spanning doorways; angled and concave regions;
overlapping sectors, dynamic adjoins, camera transitions, animated
materials, transparencies, 0x20 exemptions, scaled RLU transforms;
and more than eight candidate sectors. If a native layout disagrees
with the custom JKGM executable, validate it before activating the
shader stage.
