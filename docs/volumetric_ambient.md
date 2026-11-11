# Experimental volumetric ambient lighting

The new volumetric ambient experiment is available through
`experimental_volumetric_ambient=true`. It is opt-in and off by default.
The older `experimental_world_vertex_ambient` spatial-probe mode remains
available for A/B visual comparison. The volume is implemented in two
batches: CPU generation/validation (A), and opt-in GPU 3DO shading (B).
Ordinary rendering is unchanged while both switches are disabled.

## Agreed lighting policy

- Build once per native world snapshot from **authored Gouraud surface
  vertex intensities**. Use existing SithWorld/mesh data, skip sky surfaces
  and passable portals, and preserve native 0..3 lighting-mode behavior.
- Use a **sector-local 2x2x2 lattice**: eight quantized byte samples and
  a sector bounding box, sorted by stable sector ID. Generate smoothed
  corner values using the world vertices belonging to that sector.
- Fixed range is `[0,1]` when `enable_baked_overbright=false` or `[0,2]`
  when enabled. Never infer an exposure range from the scene.
- **Do not bake sector AmbientLight** (Thing-only), sector ExtraLight
  (the world minimum-light floor), global ExtraLight, point lights,
  or moving COG lighting fades. Initially **exclude face ExtraLight**
  from the volume as well; preserve it in ordinary world shading.
- At runtime, sample the volume with **per-vertex trilinear interpolation**
  on eligible GPU-cached 3DOs. Keep the existing pixel-light system and
  per-vertex sector-crossing selection unchanged in their respective modes.
- Apply current sector ExtraLight as a **live floor**, not an additive
  permanent part of the volume. Follow the existing vanilla 1.0 / HDR
  2.0 authored limits and preserve the November 11 saturation fix.
- Do not change native fallback, camera HUDs, skybox/0x20 Things, sprites,
  projected decals, or world geometry rendering.

## Batch A: CPU field and validation

`renderer/volumetric_ambient.hpp` contains the standalone
`volumetric_ambient_field` builder, native-world Gouraud capture
function, and trilinear CPU reference sampler. The source vertices are
discarded after the field is quantized; the persistent lighting payload
is exactly eight `uint8_t` values per represented sector (plus bounds
and sector metadata). It is built only on explicit capture, not per
frame. Invalid samples and sector pointers are ignored safely.

`tests/volumetric_ambient.cpp` and
`tests/volumetric_ambient.py` cover quantization, interpolation,
sector isolation, legacy overbright saturation, source layout,
excluded lighting terms, and absent-sector fallback. CI runs these
tests but production shader paths are unchanged by Batch A.

## Batch B: opt-in GPU integration

- `stage_volumetric_ambient()` captures/rebuilds only on world-cache
  revision or overbright-range changes. No per-frame world geometry scan.
- The owning sector is selected on the CPU using the Thing's native
  sector ID. Each eligible 3DO instance snapshots eight quantized
  samples, its bounds/range, and current sector+global ExtraLight floor.
  Shader uniforms are updated only when the drawn Thing changes.
- `gpu_3do_preview.vert` reconstructs world XYZ from the camera axes
  and existing model-view-transformed vertex; it trilinearly samples
  the eight values **once per vertex**, before the existing baked gamma
  and Gouraud interpolation. Existing per-pixel dynamic lights continue
  through the original fragment path.
- Sector + global ExtraLight is read live each frame as a separate floor,
  bounded by the selected authored lighting range. Changes to
  `SetSectorLight` do not rebuild the volume.
- Missing sectors, invalid live floor reads and excluded model kinds
  retain native camera ambient. Skyboxes/0x20, HUDs, sprites, projected
  decals, and shadow-only Things are unaffected.
- `enable_sector_crossing_lighting=true` takes precedence over the
  volume; the volume takes precedence over the old world-cell probe mode
  when both experiments are selected. Both old experiments remain
  otherwise unchanged for A/B tests.
- Runtime validation is still needed: review indoor/outdoor light
  gradients, adjoining rooms, visible vertex interpolation, and CPU/GPU
  timings before considering the new field the default. Current
  sector selection does **not** blend neighboring volumes across
  passable portals; this remains a comparison point, not a promised
  feature of Batch B. With the experiment off, the restored vanilla
  lighting baseline is unchanged.
