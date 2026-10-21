# Thing-node COG getters and Mysteries of the Sith sphere-tree collision

Investigation against the OpenJKDF2 reconstruction (JK/MotS branches),
October 2026. **Research and isolated CPU helpers only; not live gameplay
hooks or new registered COG verbs.** In-game verification can wait until
the JKGM function layout is positively identified.

## Thing-node transforms

Relevant engine code:
- [rdModel3_GetMeshMatrix](https://github.com/shinyquagsire23/OpenJKDF2/blob/master/src/Primitives/rdModel3.c)
  accepts an `rdThing*`, its WORLD placement matrix, a hierarchy node
  index, and an output `rdMatrix34*`. It checks `nodeNum < numHNodes`,
  refreshes joint matrices through `rdPuppet_BuildJointMatrices` if the
  render frame is stale, and returns `paJointMatrices[nodeNum]`.
- [rdThing_AccumulateMatrices](https://github.com/shinyquagsire23/OpenJKDF2/blob/master/src/Engine/rdThing.c)
  composes parent pivots and child transforms. This is not equivalent to
  reading the raw static node position in the model.
- [rdMatrix_ExtractAngles34](https://github.com/shinyquagsire23/OpenJKDF2/blob/master/src/Primitives/rdMatrix.c)
  extracts PYR angles, but Euler representations may jump at ambiguous
  orientations; returning the raw R/L/U axes is more reliable.

Proposed COG verbs, all operating on the **animated world pose**:

    vector pos = GetThingNodePos(thing, node);
    vector r = GetThingNodeRVec(thing, node);
    vector l = GetThingNodeLVec(thing, node);
    vector u = GetThingNodeUVec(thing, node);
    vector pyr = GetThingNodePYR(thing, node);

The COG VM returns one vector per call, so `GetThingNodeRLUVecs` cannot
naturally return all three as a vector array. Normalizing or flipping
node axes unconditionally would break valid mirrored 3DOs. Whether
`GetThingNodePos` should report the **mesh-transform origin** or the
**hierarchy-node pivot** must be specified; the first is trivial from
the output matrix, while the second may require transforming pivot
coordinates as well.

`renderer/thing_node_math.hpp` implements the read-only matrix extraction,
world-point transform, and composition for a future
`AttachThingToThingNode(child,parent,node)`. For an attachment, update
both child translation **and** rotation from the current node matrix, not
only the orientation, or the child will not orbit a rotating parent.
Runtime attachment needs lifetime/sector, detach, save/load, and network
synchronization handling.

**JKGM accessor verified in the supplied PE:** `rdModel3_GetMeshMatrix`
starts at `0x00445FD0` (not OpenJKDF2's `0x00445D80`). The function
checks the requested node against the model's `numHNodes`, compares
`rdThing.rdFrameNum` against `rdroid_frameTrue` at `0x005703D8`,
calls `rdPuppet_BuildJointMatrices` at `0x0043F160` if stale, and copies
12 floats (`rdMatrix34`) from `rdThing.paJointMatrices[node]`.
The native `rdMatrix_ExtractAngles34` is `0x00440950`; it handles
engine PYR conversion. Both entry points are protected by exact first-16-
byte checks and the COG handlers additionally validate Thing, model,
node range, world placement and output finiteness.

**Five registered JKGM verbs in `renderer.dll` (development):**

    vector origin = GetThingNodePos(thing, node);
    vector rvec = GetThingNodeRVec(thing, node);
    vector lvec = GetThingNodeLVec(thing, node);
    vector uvec = GetThingNodeUVec(thing, node);
    vector pyr = GetThingNodePYR(thing, node);

Each returns the current world-space node transform; the getters return
`(0, 0, 0)` on invalid Thing, unsupported executable, missing 3DO,
out-of-range node, or invalid matrix. COG arguments pop `node` first
and then `thing`. The position is the transformed mesh-node origin.
The R/L/U vectors retain native scale/mirroring. Angle extraction can
have normal Euler discontinuities and is not a substitute for the
basis vectors when implementing attachments.

This validates the machine code and function entry points, **not**
yet the COG behavior in a live level; that still needs testing.

## MotS collide=2 is not polygon-perfect mesh collision

From [sithIntersect_CheckSphereThingIntersection and
sithIntersect_TreeIntersection](https://github.com/shinyquagsire23/OpenJKDF2/blob/master/src/Engine/sithIntersect.c):

- `collide=0`: none, `1`: a single bounding sphere,
  `2`: **sphere tree**, `3`: face-level collision.
- `collide=2` is MotS-only in this reconstruction
  (gated on `Main_bMotsCompat`). Setting `collide=2` in our JK executable
  alone does not implement it.
- A broad-phase test uses the `treeSize` parameter. After passing
  it, the solver walks the model hierarchy.
- Every nonempty mesh node gets a sphere centered at the **current
  animated mesh matrix origin**, radius `rdMesh.radius * 0.75`.
- Amputated hierarchy branches are skipped. The solver picks the
  closest intersected sphere and derives the contact normal from its
  center.
- The implemented path returns a collision with the Thing but does
  not itself return the particular struck mesh/face in the way that
  face collision does.

For low-poly cars and characters this is a promising middle ground:
it is more faithful to articulated bodies than one big sphere, yet
cheaper than sweeping against every polygon. It does **not** solve
wheel-level or body-panel-accurate impacts.

`renderer/mots_sphere_tree.hpp` is a pure, independently testable
reference for narrow-phase sphere-tree casts. Legacy mode retains MotS's
0.75 mesh-radius multiplier and origin-centered sphere; an optional
geometry-centered mode uses fitted spheres without shrinking them. Both
follow animated matrices and inherited amputation. Neither is wired
into the Sith collision dispatcher.

Next work:
1. Test all five registered node getters on a 3DO with at least two
   animated hierarchy nodes, including a Thing culled offscreen.
2. Implement opt-in node attachments using per-frame world matrices,
   local child transforms and clean detach/destruction handling.
3. Before any `collide=2` hook, establish a safe way to read geometry and
   per-node radii, identify existing collision ABI/output semantics,
   and preserve type-1/type-3 behavior and multiplayer state.


## JKGM binary compatibility audit (2026-10-20)

The supplied `jkgm.exe` and community-patch `JK.dll` were inspected
without executing or modifying them. SHA-256 fingerprints:

- `jkgm.exe`: `afb4605c8259075c151ad6b034b98562be87ebe90369d4d9abaee09b396ed50f`
- `JK.dll`: `e86a021a41d399d4c95c96b9746d8308b3166abacfb7c7ba7794aace4fd377ca`

The uploaded executable matches the *development* branch's two
16-byte native getter prologues at `0x00445FD0` and `0x00440950`.
Disassembly at `0x00445FD0` independently confirms the hierarchy
node-count check (`rdModel3 + 0x54`), frame-count comparison against
`0x005703D8`, conditional call to `0x0043F160` to refresh the puppet
joint matrices, and copy of 12 floats for the result.

The supplied DLL exports `InitNewCogVerbs` at ordinal 1 and
`InitNewCogMessages` at ordinal 12, as expected by the renderer hooks.

To repeat these read-only prerequisite checks for future builds:

```sh
python3 tests/verify_thing_node_binaries.py /path/to/jkgm.exe /path/to/JK.dll
```

**Collision entry-point identification (static, not yet hooked):**
The original JK collision address `0x005080D0` listed as
`sithIntersect_CheckSphereThingIntersection_ADDR` in OpenJKDF2's
`sithIntersect.h` is unrelated in this JKGM binary. The likely
corresponding JKGM function starts at **`0x00508360`**, with prologue
`83 EC 4C 8B 44 24 68 53 55 56 57`. This identification is
supported by the `colflags & 0x80` check, comparisons of
`SithThing.collide` at offset `+0x28` against `3` for both objects,
`SithThing.collideSize` at `+0x30`, the sphere-test call to
`0x005087D0`, and the following mesh-face branch. Unlike MotS's
dispatcher, its leading branch does not recognize `collide=2`;
this mode currently falls through the ordinary non-face path.
The routine appears to end near `0x005085F9`.

This is a **strong static match, not a live-hook guarantee**.
Verify function signature, collision-record semantics, all callers,
and detour safety before installing an opt-in collision override.
In particular, never apply OpenJKDF2's original vanilla addresses
directly to this patched executable.

These checks establish the correctness of the *specific static binary
prerequisites*, not successful COG registration or correct output in a
running game; the latter still requires an animated-3DO runtime test.

## Collision dispatch and record layout (static JKGM audit)

A complete disassembly of the supplied `jkgm.exe` identifies one direct
`call 0x00508360`, at **`0x004E8888`**, in the collision traversal code.
The call site pushes eleven arguments and then executes `add esp, 0x2C`.
The argument count and parameter order are consistent with
OpenJKDF2's reconstructed
`sithIntersect_CheckSphereThingIntersection` signature:

```c
int CheckSphereThingIntersection(
    SithThing *movingThing, rdVector3 const *start,
    rdVector3 const *direction, float maxDistance, float movingRadius,
    SithThing *checkedThing, int flags, float *hitDistance,
    rdMesh **hitMesh, rdFace **hitFace, rdVector3 *hitNormal);
```

The call is followed by `test eax,eax` and, on success, a write into a
collision-record array. The corresponding code at `0x004E88BD` uses
a **0x28-byte record stride** and an observed maximum of **128 records
per list**. Relevant observed fields are:

| Offset | Evidence at call site | Likely meaning |
| --- | --- | --- |
| `+0x00` | collision return value OR 1 | collision flags |
| `+0x04` | checked Thing pointer | hit Thing |
| `+0x08` | zero | unused/other collision type |
| `+0x0C` | mesh output pointer | hit mesh |
| `+0x10` | face output pointer | hit face |
| `+0x14` | three floats from normal output | hit normal |
| `+0x20` | float from distance output | hit distance |
| `+0x24` | zero | reserved/other field |

These are **static observations, not an authorized ABI for a detour**.
The return flags and lifetime/ownership of hit mesh and face still need
review before implementing a collision hook. In particular, the stock
MotS sphere-tree path does not report a specific polygon hit.

### Type-2 collision without treeSize (JKGM proposal)

MotS uses `treeSize` only as a broad-phase radius before its
per-node sphere tests. It is **not** needed to construct those spheres:
the 3DO parser already reads each mesh's `RADIUS` into `rdMesh.radius`
(`rdModel3_LoadEntry`), and MotS tests that value multiplied by
`0.75` at each current joint-matrix origin.

The `rdModel3_CalcRadii` postprocess calculates additional **expanded
model** radius data from local mesh vertices and hierarchy positions.
It does *not* replace the per-mesh authored `rdMesh.radius` field.
A joint-origin mesh sphere is often oversized when an artist stores a
knee/elbow/etc. at the joint but places most geometry away from it.

Our first compatibility goal is a separate opt-in `collide=2` mode
requiring **no** additional per-Thing `treeSize` property.
`renderer/mots_collision_dispatch.hpp` therefore keeps the 1/2/3
mode selection but marks type-2 checks with `skip_broadphase=true`.
The selection retains flag `0x80` forcing sphere-only handling,
type-2 precedence over type 3 when enabled, and reversed geometry
selection with the checked Thing's `collideSize` used as the
narrow-phase moving radius. It is a testable selector, **not** a
live engine collision replacement.

To avoid doing expensive per-mesh checks for every candidate forever,
`mots_tree_enclosing_radius` can *derive* a conservative broad-phase
radius from the active animated mesh spheres' centers relative to the
Thing's origin. This requires no special 3DO or template field, and is
separate from the selector until the hook ABI has been verified.

### Optional geometry-centered mesh collision spheres

`renderer/mots_mesh_collision_bounds.hpp` contains
`mots_center_mesh_sphere(vertices,out)`, which returns:
- Local center = midpoint of the mesh vertex AABB.
- Radius = farthest vertex distance from that center, rounded outward
  to avoid floating-point underestimation.

Unlike blindly multiplying the 3DO's origin-centered radius by `0.75`,
the computed radius encloses all supplied vertices. It is often
substantially tighter for long, joint-origin limbs and appendages.
Its purpose is a conservative **collision sphere**, not a minimum
bounding sphere or a replacement for polygon collision.

`mots_tree_node.center_on_geometry` selects this optional mode:
`local_center` is transformed by the *current animated mesh matrix*;
`mesh_radius` must be the corresponding fitted radius, **not** the
original 3DO Radius. For native mode leave this flag false, supply
the original Radius, and the historical MotS `0.75` multiplier is
preserved. In centered mode, mirrored/orthogonal nonuniform scales
affect sphere size conservatively. The helper falls back to a
conservative Frobenius bound for unusual sheared matrices.

The model's **actual pivot and KEY animation are unchanged** by this
collision-only recentering. The 3DO HIERARCHYDEF `pivotx/y/z` is
an offset in the node's geometry/transform relationship, not simply
a new location for its joint. OpenJKDF2's
[`rdThing_AccumulateMatrices`](https://github.com/shinyquagsire23/OpenJKDF2/blob/master/src/Engine/rdThing.c)
builds the final matrix using the node pivot and compensating parent
pivot before combining it with the parent placement. Therefore the
matrix returned by `rdModel3_GetMeshMatrix` **already includes pivot
effects**. To center the collision sphere, transform its mesh-local
center by that final animated matrix **exactly once**; do not separately
add/subtract a node pivot or use the node's world-space matrix origin
as a proxy for the actual joint.

This distinction also applies to our existing
`GetThingNodePos` COG getter: it currently reports the origin of the
*animated mesh transform*, which need not coincide with a physical
joint when nonzero pivots are involved. A separate joint-position
getter would require an explicitly verified definition of the
node's joint point and parent/child pivot semantics.

A future Blender Sith exporter **Correct Pivots** mode can recenter
vertex coordinates and adjust corresponding HIERARCHYDEF pivot data
so the rendered, animated result stays unchanged. It must coordinate
mesh vertices, hierarchy-relative transforms and children, and
preserve the existing joint behavior: changing the pivot value
alone would alter the appearance/animation. This feature is
independent of sphere-tree collision; no exporter changes are needed
for the optional collision-only fitted centers.

Relevant unit tests in development CI:
`tests/mots_collision_dispatch.cpp`,
`tests/mots_sphere_tree.cpp`, and
`tests/mots_mesh_collision_bounds.cpp`.
These test mathematical helpers only; no JKGM gameplay collision
behavior has been modified.

**Confirmed layout incompatibility:** MotS's OpenJKDF2
[`SithThing` definition](https://github.com/shinyquagsire23/OpenJKDF2/blob/master/src/types.h)
contains an extra conditional `treeSize` member and an additional
earlier field, so it is not binary-compatible with JKGM's
`SithThing`. We explicitly do **not** write a MotS `treeSize`
offset or change the JKGM structure layout.

**Remaining gates before runtime type 2:** confirm `SithThing`,
`rdModel3`, `rdMesh`, geoset and amputation layouts in this JKGM
build; select the appropriate per-Thing render geoset; provide correct
local vertices and node matrices; validate callee ABI, caller flags,
hit-record semantics, and safe detour lifetime; then add an opt-in
collision hook preserving original collision modes `1` and `3`.
No additional template parameter is required.

## Read-only JKGM mesh-geometry snapshot (development)

`renderer/mots_native_mesh_snapshot.hpp` separates two steps:

- `read_native_collision_model(reader, thing, out)` reads the selected
  3DO geoset, mesh-local vertices, authored radii, hierarchy links,
  node pivot offsets, and amputation flags. It never invokes animation
  code or modifies the game. Memory reads use validated pointers and
  64 KiB chunks, and failure preserves the previous output.
- `build_native_collision_tree(snapshot, poseReader, preferFitted, out)`
  uses a separate pose provider for **current world-space animated
  matrices**. It honors amputations and supports the experimental
  geometry-centered bounds without changing hierarchy pivots.

Offsets confirmed against the patched JKGM executable and native
mesh interception (not the incompatible MotS structure):

| Structure | Relevant fields |
| --- | --- |
| rdModel3 | geosets `+0x24`; num geosets `+0x44`; default geoset `+0x50`; node count `+0x54`; hierarchy pointer `+0x58` |
| rdGeoset | stride `0x08`; mesh count `+0`; mesh array pointer `+4` |
| rdMesh | stride `0x70`; vertex pointer `+0x30`; vertex count `+0x48`; radius `+0x54` |
| rdHierarchyNode | stride `0xB4`; flags `+0x40`; index `+0x44`; mesh index `+0x4C`; parent pointer `+0x50`; pivot `+0x60` |
| rdThing | amputation flags pointer `+0x28`; geoset override `+0x30` |

JKGM disassembly at `0x00446247`–`0x00446276` reads hierarchy
mesh index `+0x4C` and joint index `+0x44`, then computes
`meshIndex * 0x70` and `jointIndex * 0x30` before calling
`rdModel3_DrawMesh` at `0x00446360`. This independently validates
core field use. The renderer's existing native wrapper also verifies
the `0x58`-byte mesh header.

`tests/mots_native_mesh_snapshot.cpp` uses synthetic x86 address
spaces to exercise correct geoset selection, parent links,
amputation, pivots, malformed pointers, failure atomicity,
and more than 64 KiB of source vertices.

This is **not a live collision hook** and has not been tested in-game.
The pose callback must be guarded by the existing native function
signature validation before actual integration. Future geometry
caching must also detect asset replacement and COG mesh mutations.


## Type-2 query staging and experimental engine detour

`renderer/mots_native_collision_query.hpp` now joins the previous
read-only mesh snapshot and per-frame pose provider to an actual
sphere-tree collision query. It selects the nearest hit and exposes
three explicit outcomes:

- `hit`: valid collision with hierarchy-node index, distance,
  normal, and selected world-space sphere center
- `miss`: valid complete query with no collision, including safe
  rejection by a derived broad-phase bounding sphere
- `unavailable`: unsupported collision mode/raycast flag, invalid
  snapshot, or failed/non-finite pose; callers **must not** interpret
  this as a completed type-2 miss

`mots_probe_type2_collision` also reproduces the MotS branch where
the **moving** Thing owns the sphere-tree geometry: it reverses
the ray, casts from the checked Thing's position using its collision
radius, then negates the resulting normal. When the checked Thing
owns the type-2 hierarchy the ray runs forward normally. A pair
including types 2 and 3 selects type 2 as in MotS. Flags other than
zero (including the special `RAYCAST_80` forced-sphere case) and
collision modes 1 or 3 do **not** enter this implementation. The
native engine must continue handling those cases unchanged.

The derived sphere envelope is recomputed from the current animated
pose. For a moving sphere, an optional conservative double-precision
segment-to-center calculation skips the narrow phase only when the
cast is **definitely outside** that envelope; tangent or invalid
bounds are never culled. Sphere intersection arithmetic also uses
double precision internally, preventing `INF-INF`/NaN hit distances
from very large but finite model coordinates.

The associated tests are
`tests/mots_native_collision_query.cpp` (mode arbitration, reverse
casts, nearest mesh, broad-phase culling, invalid pose, amputation),
`tests/mots_native_mesh_snapshot.cpp` (synthetic native
bytes -> fitted vertices -> animated collision), and
`tests/mots_sphere_tree.cpp` (large-coordinate robustness).
All tests run under development CI.

### Gates before enabling native JKGM collide=2

The identified `sithIntersect_CheckSphereThingIntersection` entry
is `0x00508360`, called from `0x004E8888`; static disassembly
supports the 11-argument cdecl ABI, but there is no detour yet.
A future opt-in detour must:
1. Validate the exact executable signature and all Thing pointers,
   keep a trampoline to the original function, and intercept only
   specifically enabled type-2 candidates with understood flags.
2. Snapshot the owning model and retrieve animated world-space poses
   via the already validated native `rdModel3_GetMeshMatrix` accessor.
   No render-culling dependency is allowed for collision.
3. Write a valid hit distance and normal with `SITHCOLLISION_THING`
   status, preserving native mesh/face output conventions. Return
   definitive `miss` only after an entire valid type-2 test;
   `unavailable` must fall back to the original engine.
4. **Finish auditing sector-level candidate selection.** Static
   disassembly of JKGM's Thing candidate traversal at
   `0x004E86C0` shows a linked-list iteration of Things at
   `SithThing + 0x60`, checks for the Thing collision mode at
   `+0x28` (rejecting zero), and then calls `0x00508360` from
   `0x004E8888`. There is **no direct collideSize radius
   comparison in that visible Thing loop**, which is encouraging.
   However the *earlier sector traversal and spatial candidate
   discovery* have not yet been proven independent of
   `collideSize`. If any upstream broad phase omits candidates with
   undersized collideSize, the type-2 hook cannot recover them merely
   by replacing the narrow phase.
5. Establish stable mesh snapshot caching/invalidation rules for
   COG vertex edits, mesh replacement, geoset changes, animation,
   amputation, and entity destruction. Snapshotting thousands of
   vertices on every collision probe is safe as an experimental
   reference but **not** an acceptable final hot-path cost.

The reference query is now bridged into an **opt-in native detour** in
`renderer/main.cpp`, at `0x00508360`, with the exact x86 cdecl
prototype and a verified original prologue. This is OFF by default:
`"enable_mesh_collision": false` in `jkgm.json`. On supported builds,
the enabled hook intercepts collide=2 only and uses a verified native
`rdModel3_GetMeshMatrix` pose provider; other modes keep the original
JK behavior. Unsupported flags, malformed models, invalid poses, or
unsafe output pointers fall back to the trampoline. MotS tree hits
produce the **Thing hit bit, hit distance and normal** but leave
`ppHitMesh` and `ppHitFace` unchanged; only polygon collision
populates them. A confirmed tree miss returns zero.

The initial version read and fit ALL model vertices on EVERY type-2
probe; an in-game log showed ~600 ms JK-side frame stalls around an
animated actor. A bounded `mots_collision_snapshot_cache` now retains
the fitted hierarchy, refreshes instantaneously on Thing/model/geoset
identity changes, reads limb amputation flags live and refits in-place
vertex changes at most 1 second after mutation. Current animation
matrices still refresh per query. The cache's 24-entry LRU prevents
unbounded model retention; models over 262144 total vertices fall back
to native handling. Geometry edits may be stale for <=1s: a follow-up
requires more precise geometry invalidation. The source remains opt-in until live gameplay
validation establishes its stability, candidate coverage, and
collision response. See `doc/install.md` for the test switch,
fallback behavior and bounded diagnostic logs.

The native intersection routine accepts general raycast flags; only
`0x80` forces sphere collision and `0x400` changes an overlap
distance convention. Those two special flags continue to use the
original native function. Outer caller flags remain intact, including
ordinary combinations used during movement.
