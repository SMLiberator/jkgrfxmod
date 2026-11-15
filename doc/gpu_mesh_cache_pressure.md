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

## On-demand COG mesh-source validation (SetModelMesh scaling fix)

The 16 wrapped JK.dll model verbs continue to advance the original global
collision/Type-3 revision. The GPU mesh cache now uses two separate
constant-time mutation epochs, instead of walking every cached rdMesh:

- Full-source epoch: SetModelMesh, ClearModelMesh, OverwriteModel and
  ReloadModel. On the next actual GPU draw/capture of a cached mesh, it
  revalidates native source memory, hashes source bytes and rebuilds the
  CPU snapshot only if bytes changed.
- Material epoch: ChangeModelMaterial, ReplaceModelMaterial and
  ReplaceModelMeshMaterial. The next use of a mesh first checks only
  its face MAT/cel signature; the full source is rebuilt only if changed.
- Clones, append calls and node swaps retain their existing per-mesh
  header comparison and new-cache-key detection.

Both the main camera and independent planar-reflection camera share
the same epochs and snapshot cache. No mutation scans hidden/unused
mesh entries, so cost does not grow with historical cache population.
Only meshes actually used AFTER a source mutation need source rehashing.

Every 32 frames, the integrity audit still detects untracked in-place
writes and can revert to per-frame hashing. Deferred source rechecks
guard against buffers decommitted earlier in the same frame.

Profiling with profile_renderer=true:

- PERF gpu_3do lazy_mutations: face-material rechecks, differences and
  CPU cost (no global scan).
- PERF gpu_3do hash_causes_total: full-source revision hash counts.
- PERF gpu_3do source_check_us_avg: main-camera full-source hash cost.

Suggested A/B: accumulate thousands of cached models, call SetModelMesh
repeatedly on a small set of actors, and compare CPU times with an older
build. The cost should no longer track unused cache size. If many
distinct active meshes are simultaneously rendered, their on-demand
source rehashes are still necessary until we implement a verified
model-specific mutation key.
