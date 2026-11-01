# Per-pixel dynamic lighting on GPU-cached geometry (experimental)

`gpu_pixel_lighting` selects the dynamic-lighting method for **all supported
GPU-cached world geometry and 3DOs**, not just models. It does not affect
the native JK CPU/D3D geometry path.

```json
{
  "use_model_cache": true,
  "world_cache_preview": true,
  "gpu_pixel_lighting": true
}
```

The switches are independent: enable only the caching type(s) you want.
`gpu_pixel_lighting` defaults to `false`.

| Cached geometry | `gpu_pixel_lighting=false` | `gpu_pixel_lighting=true` |
| --- | --- | --- |
| 3DO meshes | JK-style Gouraud dynamic lighting evaluated at vertices | Dynamic point lights evaluated per fragment |
| SithWorld architecture | JK native per-world-vertex dynamic light data, interpolated as Gouraud | Dynamic point lights evaluated per fragment when camera data is valid |
| Native fallback geometry | Native JK rendering | Native JK rendering |

## Shared editable pixel-light formulas

Both GPU-cached pixel-light fragment shaders include:

`assets/shaders/jk_point_lighting.glsl`

- `jk_point_sample()`: radius cutoff, attenuation, face incidence and
  nonnegative point-light contribution.
- `jk_point_compose()`: combine that contribution with the **already
  interpolated, gamma-corrected baked RGB**, including saturation.
- `JK_POINT_GAMMA`: shared gamma exponent (initially `1.8`).

`renderer/shaders.cpp` expands local GLSL includes at load time. Changes
to the shared file apply to both pixel-light paths after restarting the
renderer; no C++ recompilation is required.

The baked/static colors—including authored face lights, sector effects and
model vertex colors where applicable—are evaluated at vertices *before*
raster interpolation. Pixel lighting preserves that baked result when no
dynamic light contributes. Do not replace the baked result with
`pow(interpolated_raw_vertex_intensity, gamma)`: the nonlinear interpolation
difference causes visible bands around strongly contrasting baked vertices.

With pixel lighting disabled, 3DOs retain their separate vertex-shader
Gouraud calculation and the cached world uses the original JK world-vertex
dynamic-light buffer; changing the shared pixel formulas must not alter
those fallback modes.

## Receiver-specific behavior

3DOs use local positions and normals (accounting for scaled/mirrored
transforms); their legacy non-Gouraud Diffuse mode uses the face anchor.
World surfaces use static world-space positions and authored face normals.
Their eligible Gouraud point-light samples share the same attenuation and
composition formula as 3DO Gouraud samples.

Point lights are scalar/neutral in this implementation. Per-pixel point
lights do not introduce independent point-light shadow maps. Camera HUD
Things and projected decals retain their dedicated rendering behavior;
unsupported native geometry does not receive this shader.

## Validation

Compare both caching types with the same moving dynamic light and
`gpu_pixel_lighting` toggled. Check bright and dark baked vertices,
the radius boundary, zero-light frames, face incidence, scaling, fades,
materials and performance. `tests/gpu_pixel_lighting.py` verifies source
wiring and representative formula edge cases. These tests do not replace
in-game visual parity checks.
