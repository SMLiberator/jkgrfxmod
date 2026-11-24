# MatCap and clearcoat material layers

JkGRfxMod adds independent MatCap and clearcoat layers to **GPU-cached world and 3DO materials**. Both are authored through material-pack properties and can coexist with ordinary environment reflections.

## MatCap

A MatCap projects an image according to the material's view-space normal. It is useful for stylized surfaces, iridescent paint and artistic highlights, independently of `reflection_mode`.

```json
{
  "matcap_map": "pearlescent.png",
  "matcap_factor": [0.7, 0.5, 1.2],
  "matcap_roughness": 0.3,
  "matcap_lighting_influence": 1.0,
  "matcap_surface_map": "pearl_mask.png"
}
```

- `matcap_factor` multiplies the MatCap RGB image; components range from 0 to 4.
- `matcap_roughness` controls mipmap-based blur (0 to 1).
- The optional linear `matcap_surface_map` uses R for intensity and G for roughness modulation.
- `matcap_lighting_influence` controls how strongly the MatCap follows scene lighting (0 to 1; default 1). At 0 the layer remains artistic/unlit; at 1 it responds to available authored and dynamic lighting/shadowing.
- Material normal maps affect MatCap orientation. The MatCap itself does not add physical GGX highlights or emit light.
- MatCap images are color textures (sRGB), while their surface maps are linear data.

The layer requires GPUs with enough fragment sampler units for the additional maps; on hardware without that capacity, ordinary material rendering remains available.

## Clearcoat

Clearcoat adds a second dielectric specular lobe, with its own roughness and optional normal map, over the base material. It shares the base material's available environment or realtime capture source but computes separate highlights.

```json
{
  "clearcoat_strength": 1.0,
  "clearcoat_roughness": 0.08,
  "clearcoat_transmission": 0.65,
  "clearcoat_surface_map": "wet_asphalt.png",
  "clearcoat_normal_map": "water_surface.png"
}
```

The linear clearcoat surface map uses R = strength, G = roughness, B = transmission, each multiplied by its material scalar. With no surface map, strength defaults to 0; with one, it defaults to 1. Roughness defaults to 0.1 and transmission to 1.

`clearcoat_transmission` controls attenuation of underlying diffuse and MatCap color where the coating is present. The separate coat reflection remains visible, and emission is not attenuated.

## Runtime COG controls

```c
SetMatClearcoat(material, VectorSet(strength, roughness, transmission), fadeTime);
current = GetMatClearcoat(material);
ResetMatClearcoat(material, fadeTime);
```

`SetMatClearcoat` animates **absolute** strength, roughness and transmission; interruptions continue from the current interpolated values. `ResetMatClearcoat` returns to the authored defaults, and `GetMatClearcoat` reads the current values. Overrides follow MAT identity and are cleared on world changes.

See [material-pack compiler additions](compile.md) and [reflection captures](offscreen_captures.md).
