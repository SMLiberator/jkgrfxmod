# Experimental volumetric ambient lighting

The new volumetric ambient experiment replaces the obsolete
`experimental_world_vertex_ambient` spatial-probe method **after** its
separate stages are validated. Batch A below introduces only CPU data
generation and tests; it does **not** enable or modify in-game lighting.

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
  per-vertex sector-crossing selection available independently.
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

## Batch B: runtime and GPU integration (not yet implemented)

- Capture/rebuild the volume when the world-cache snapshot changes.
- Upload the compact sector array and select the appropriate volume at
  each 3DO vertex; interpolate its eight corners on the GPU rather than
  per-fragment.
- Combine the interpolated baked floor with the appropriate **live**
  sector ExtraLight and the existing model lights, respecting the
  native-vs-overbright lighting policy.
- Respect passable sector connections, existing sector-crossing choices
  and skybox/HUD exclusions; fall back to native camera ambient where
  the volume has no valid sector data.
- Replace the old `experimental_world_vertex_ambient` option with
  the new opt-in experiment **once** the new path is testable; do not
  silently change default visuals.
- Compare the same scenes for stability, visual quality and frame time
  before removing the old code and its tests.
