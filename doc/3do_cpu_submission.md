# CPU-side 3DO submission reuse

The main GPU-cached 3DO renderer prepares one lightweight **per-Thing-node
submission context** as it collects eligible visible draw ranges. The context
contains the existing modelview matrix, its camera-local eye, the validity of
that eye, the 3DO's reversed-winding bit, and a lazily resolved GPU mesh
pointer. All accepted groups of the same Thing use that same context.

This does **not** move native model traversal, pose updates, visibility
selection, material readiness checks, or GPU mesh uploads earlier. A GL mesh
is still looked up and uploaded only after a real draw survives culling,
classification and sorting. It is looked up and last-used stamped once per
Thing context, not once per opaque/transparent material range. The pointer
refers to a heap-owned mesh, not an unordered-map element, and remains valid
across map rehash and normal opaque/alpha submission.

The ordinary opaque pass already builds its alpha candidate list. The new
ordinary submission contexts remain alive until the alpha pass consumes that
list; decal and post-exempt HUD passes have separate scratch vectors.
Consequently the shared matrix, camera-local eye, winding and GPU mesh can
all be reused without changing the original transparent painter order.
Sprite instancing, mixed-alpha routing, saber trail ordering, shadows and
per-Thing shader/lighting uniforms remain unchanged.

The native model hook also defers reading, validating and copying the source
3DO filename until a fallback/error diagnostic requests it. It preserves the
old nested model-hook context, including already resolved names, and still
reads the authoritative rdModel3 pointer for decals/LOD/diagnostics.

These internal CPU optimizations require no JSON keys. Existing PERF logs
(`opaque_collect`, `opaque_submit`, `alpha_submit`, native model/mesh
inclusive timings and draw-call counts) remain available for a future A/B
benchmark. No performance gain is claimed without an in-game measurement.

**Not changed:** Per-frame sorting of opaque material groups. Precomputing
a cached group's order requires a separate proof that it preserves the same
opaque ordering without disturbing alpha's source-order tie breaks; this
belongs in a subsequent change rather than this low-risk CPU cleanup.
