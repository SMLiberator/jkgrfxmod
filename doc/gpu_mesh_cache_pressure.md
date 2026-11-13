# GPU 3DO CPU cache pressure experiment

This experiment applies to the `performance` branch. It targets performance
loss after a high-altitude trip through a very large procedural JkGR world.

## Changes

The CPU geometry snapshot cache is shared by the ordinary native mesh hook
and independent planar-reflection node discovery. The old 4,096-entry
full-clear / planar hard-fail limit is replaced with one selective policy:

- Soft limit: 8,192 mesh entries.
- Pressure reclaim target: 7,168 entries.
- Only entries **not referenced for more than 128 rendering frames** are
  eligible, oldest first.
- Sweep at most once per rendering frame and only under pressure.
- When all entries are recent, temporarily exceed the soft limit rather
  than drop currently visible or reflected geometry.
- World/renderer resets still clear the cache intentionally.

Each CPU entry has `last_used_frame`. Instance geometry is held by
`shared_ptr`; GPU uploads are kept separately by unique, never-reused
geometry IDs and the existing GPU-side 64-frame idle cleanup.

## Performance counters

With `profile_renderer=true`, look for two new two-second log lines:

- `PERF gpu_3do mesh_cache`: average entries, session peak, soft limit,
  average hits/misses, total targeted evictions and deferred sweeps.
- `PERF gpu_3do hash_causes_total`: new cache entry, changed header,
  global mesh revision, 32-frame audit and per-frame fallback.

The existing `PERF gpu_3do` lines also report source hashing CPU time,
new snapshots and total GL uploads. Hash-cause totals include independent
planar capture; the legacy `source_check_us_avg` covers the native
primary renderer only.

## Suggested A/B

From a fresh level entry, capture a stable viewpoint, fly above the map until
the full city is exposed, and return to **exactly** the original viewpoint.
Compare FPS, entry count, cache misses, evictions, source hash causes,
snapshots, GL uploads, and GPU transparent-pass time. Use the same settings
for runs with the eviction change and the prior build.

This change does **not** introduce fine-grained COG mesh mutation tracking.
JK.dll mutation verbs still bump the global revision, so frequent unrelated
edits may legitimately explain continued expensive source rehashing.

## Targeted COG mesh-source invalidation (follow-up)

The 16 wrapped JK.dll model verbs still advance the **original global**
revision counter used by the precise Type-3 collision cache. GPU geometry
snapshots now use a separate source generation, so clones, append operations,
hierarchy reference changes and header-changing mesh swaps no longer force
every cached mesh to hash its complete contents.

In-place face material edits from `ChangeModelMaterial`,
`ReplaceModelMaterial` and `ReplaceModelMeshMaterial` schedule one
coalesced material-pointer scan at the next primary/offscreen 3DO lookup.
Only CPU snapshots with actually changed face material references are
discarded. `SetModelMesh` and `ClearModelMesh` (commonly issued repeatedly
during a model rebuild), along with `OverwriteModel` and `ReloadModel`,
schedule a coalesced full-source comparison at the next mesh-consumption
boundary. Thousands of COG calls collapse to the same pending dirty bit
until that point; only actually changed cached meshes are discarded.
This boundary is before reading the model for rendering, not literally at
the end of the JK frame: deferring beyond a draw could render stale geometry.
For mesh-slot replacements/clears, existing per-mesh header comparisons
handle pointer/count changes. Geometry is not rebuilt until it is used.

The 32-frame full-source audit remains: an unclassified in-place change
switches the renderer back to per-frame hashing to protect correctness.
The shader-facing GL resource IDs remain unique and pending draw instances
retain their own shared geometry references.

`PERF gpu_3do targeted_mutations` reports the number of coalesced scans,
entries checked/invalidated, and average scan CPU cost. In the normal
steady-state, global COG mutations should no longer inflate
`hash_causes_total revision`. Before interpreting a performance improvement,
check the selective-scan CPU cost too.

The mutation effect table lives in `renderer/native_mesh_revision.hpp`.
The implementation does not inspect COG argument stacks or assume every
handler succeeded; consequently a material scan checks the resident cache
rather than relying on an unverified model argument. For safety, the native
memory of potential released meshes is revalidated before the scan.
