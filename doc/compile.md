# JkGRfxMod material-pack compiler extensions

JkGRfxMod uses the JkGfxMod material-pack workflow and `compile.exe`. This page documents **JkGRfxMod-specific fields and behavior**, not the unchanged JkGfxMod compiler arguments, replacement matching or pack layout.

Material settings may be inherited from the pack or overridden by individual material definitions. Image maps are compiled with the appropriate color-space handling: color/emissive/MatCap images use sRGB, while normal, surface, displacement and other data maps use linear values.

## Albedo and animated emission

`use_original_albedo: true` preserves the game's native MAT image while applying replacement material effects. It cannot be combined with a replacement `albedo_map` for the same material.

`albedo_scroll` and `emissive_scroll` are independent U/V scroll rates in texture repeats per second. `emissive_pulse` multiplies `emissive_factor` over time using `sine`, `square`, `flicker` or `random` waveforms, with configurable rate, minimum, maximum and phase.

```json
{
  "name": "animated_lamp",
  "use_original_albedo": true,
  "emissive_map": "lamp_glow.png",
  "emissive_factor": [1.0, 0.6, 0.2],
  "emissive_scroll": [0.0, 0.1],
  "emissive_pulse": {
    "wave": "sine",
    "rate": 2.0,
    "min": 0.25,
    "max": 1.0
  },
  "replaces": ["lamp.mat"]
}
```

## Surface and reflection layers

- `normal_map` and `normal_map2` support independently scaled and scrolling normal layers; use `normal_strength`, `normal_strength2`, `normal_scroll` and `normal_scroll2`.
- `surface_map` supplies per-pixel reflection strength, roughness and metalness in its R/G/B channels.
- `reflection_mode` selects `envmap`, `screen`, `planar`, `hemi`, `cube` or `shared`.
- `matcap_map`, `matcap_surface_map`, `matcap_factor`, `matcap_roughness` and `matcap_lighting_influence` define an independent artistic MatCap layer.
- `clearcoat_strength`, `clearcoat_roughness`, `clearcoat_transmission`, `clearcoat_surface_map` and `clearcoat_normal_map` define the secondary clearcoat layer.
- `displacement_map` and `displacement_factor` control parallax material displacement when enabled.

See [material layers](material_layers.md) and [reflection captures](offscreen_captures.md) for behavior and restrictions.

## Transparency and depth

`alpha_mode` supports material masking and screen-door dithering. `blend_mode` controls ordinary blending and specialized additive, multiply and Fluid rendering. `fluid_thickness` sets the depth-dependent Fluid absorption coefficient.

`depth_bias` changes polygon depth testing for overlapping triangles, including GPU-cached world and 3DO surfaces. **It is not measured in Jedi Knight world units**: positive values pull the surface forward in depth testing; negative values push it back.

## Compilation

Use the normal JkGfxMod material compiler invocation with your source material-pack script and resources. Compile the pack with the `compile.exe` from the same JkGRfxMod build as the renderer so the added metadata fields are recognized.
