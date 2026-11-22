# GPU continuous ambient (experimental)

Enable `"experimental_continuous_ambient_gpu": true` with `use_gpu_cache` to test
world-aligned continuous ambient on ordinary GPU-cached 3DOs. This is a
per-vertex GL 3.3 lighting experiment; it does not alter world meshes,
sprites, decals, HUD models, or 0x20 skybox/vertex-lighting models.

The former independent CPU-only diagnostic, legacy world vertex-probe
lighting, and sector-local 2x2x2 rendering experiment have been removed.
CPU field construction remains **required** whenever the GPU experiment
is enabled: the GPU buffers cannot exist without the field.

## Capture and rendering

- `renderer/continuous_ambient.hpp`: CPU sparse field and portal stitching.
- `renderer/continuous_ambient_capture.hpp`: native authored-light capture.
- `renderer/passable_adjoin_portals.hpp`: native passable-Adjoin polygon
  validation, including invisible Adjoins absent from the rendered mesh.
- `renderer/continuous_ambient_gpu.hpp`: bounded GPU hash table.
- `assets/shaders/continuous_ambient_lookup.glsl`: GLSL 3.30 voxel lookup.
- `assets/shaders/gpu_3do_preview.vert`: per-vertex ambient sampling and
  trilinear interpolation.

The field samples world Gouraud vertices into a sparse global lattice keyed
by sector and grid coordinate. At validated finite passable Adjoins, voxels
shared by connected sectors use reconciled lighting. Unconnected overlapping
sectors retain independent values. Missing sparse voxels use each sector's
analytic eight-corner low-frequency fallback at the grid position. The
fallback's trilinear interpolation helper is intentionally preserved from
the earlier volumetric experiment.

The field rebuilds on world-cache revisions or baked-overbright changes,
not each frame. Sector ExtraLight is read live; shared brightness values are
reuploaded only when sector floors change. A snapshot logs
`CONTINUOUS AMBIENT CPU snapshot`, a GPU upload logs
`CONTINUOUS AMBIENT GPU upload`, and with `profile_renderer` enabled,
periodic coverage and instance counts are reported.

## Requirements and limitations

GPU sampling needs at least 23 fragment and combined texture units and two
vertex texture units. Texture buffers occupy units 21 and 22. On devices
without these capabilities, native ambient lighting remains the fallback.
The experiment currently does not run simultaneously with
`enable_sector_crossing_lighting`.

For eligible models the final vertex lighting uses the sampled ambient as a
**brightness floor** against authored/static and dynamic light contributions;
a model already brighter than the sampled field may look identical to
vanilla. CPU sample coverage alone does not prove GPU shader visual parity.

## Tests

CI runs the native portal-stitching capture test, the CPU field source
contracts, the GPU hash-table unit test, and GPU transport source contracts.
The old legacy-probe and independent 2x2x2 tests have been retired.
