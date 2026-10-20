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
reference for the narrow-phase sphere-tree cast. It uses the 0.75 radius
factor, world-space node origins, nearest collision, and amputation
inheritance. It does not claim binary equivalence to MotS for unusual
edge cases and is not wired into the Sith collision dispatcher.

Next work:
1. Test all five registered node getters on a 3DO with at least two
   animated hierarchy nodes, including a Thing culled offscreen.
2. Implement opt-in node attachments using per-frame world matrices,
   local child transforms and clean detach/destruction handling.
3. Only after confirming native collision entry points, add an opt-in
   `collide=2` collision hook, retaining type-1/type-3 handling,
   broad-phase behavior, contact normals and multiplayer state.
