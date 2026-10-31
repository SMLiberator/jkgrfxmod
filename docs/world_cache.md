# Native world GPU cache: phase 0 (candidate mesh probe)

This is deliberately a **non-rendering** prototype. It verifies that JKGM
can retain native SithWorld surface geometry in OpenGL buffers without
changing JK's existing sector visibility, Direct3D execute buffers, world
light colors, sky and portal behavior, material animation, or HOM.

Enable in `jkgm.json`:

```json
"world_cache_probe": true,
"profile_renderer": true
```

It is independent of `use_model_cache`, which only controls cached 3DOs.
The probe defaults to `false`.

At first successful world snapshot, watch for:

```
WORLD CACHE phase0 built ... surfaces=... uv_ready=... missing_uv=...
                        vertices=... triangles=... bytes=... revision=...
                        mode=nonrendering_native_world_unchanged
```

While profiling is enabled, every 120 rendered frames it also reports
live eligible/hidden/unreadable surfaces and detected changed MAT cels or
material pointers. None of those surfaces is submitted to the visible
framebuffer by this prototype. The original native world continues rendering.

## What is cached

- The same validated per-world static geometry snapshot used by projected
  decals and the sun/spotlight shadows, including its stable SithSurface
  index and source rdMaterial reference
- Per-vertex world positions, available MAT UVs, and flat face normals
- Persistent OpenGL indexed VBO/EBO/VAO with bounded allocations
- Per-frame *optional diagnostic sampling* of live GeoMode and material-cel
  state, not baked into the static mesh

Only the existing receiver subset is included. Passable portals, sky
surfaces, material-less surfaces and any other faces intentionally excluded
by the shadow/decal snapshot remain native. Missing-UV surfaces may be
stored for audit but are NOT considered ready for visible GPU rendering.

## Limitations and planned next step

The existing snapshot's `matches()` recognizes world pointer/topology
changes, but not arbitrary in-place modifications of world vertex positions,
UVs or material arrays. Such changes need explicit revision tracking or
dirty-range updates before the cache can become an authoritative visible
renderer. Live GeoMode and MAT cel changes are detected for diagnostics,
not yet applied to GPU draws.

Phase 1 is to extract **visible-surface selection** from JK's sector/adjoin
traversal without doing the rest of the CPU polygon pipeline. Use those
surface IDs with this immutable index buffer and current material/lighting
metadata to submit only supported opaque world surfaces; keep unsupported
faces on the native path. Prevent double-rendering, preserve HOM and verify
visual parity before extending coverage to alpha, sky, animation or
pixel-point lighting.

**Important:** Enabling `world_cache_probe` itself cannot improve
framerate; it allocates extra GPU storage for diagnostic validation.
The future `use_world_cache` visible-rendering switch should be introduced
only after surface-level visibility and single-path draw substitution work.
