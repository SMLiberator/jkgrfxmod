# Independent matcap and clearcoat material layers

**Status:** metadata/schema foundation only. The new properties are parsed
and stored but **do not affect rendering yet**. Shader binding, world/3DO
compositing, COG verbs, and the shared realtime reflection mode will follow
in separately testable changes. Older material packs retain existing visuals.

## Artistic matcap (separate from reflection_mode)

```json
{
  "matcap_map": "pearlescent.png",
  "matcap_roughness": 0.3,
  "matcap_factor": [0.7, 0.5, 1.2],
  "matcap_surface_map": "pearl_mask.png"
}
```

- Matcaps sample by view-space normal; they do not create physical specular
  highlights or consume an independent reflection capture.
- `matcap_factor` is an RGB multiplier (each component 0..4), and subsumes
  any separate `matcap_strength` parameter.
- `matcap_roughness` controls mipmap blurring (0..1), not microfacet BRDF.
- The proposed linear `matcap_surface_map` uses R=intensity and
  G=roughness multiplier, with B reserved. With no map, both multipliers
  are implicitly 1. When `matcap_map` is absent, the layer is inactive.
- For backwards compatibility, the old `"reflection_mode":"matcap"`
  continues to be recognized separately.

## Clearcoat (second dielectric specular lobe)

```json
{
  "clearcoat_strength": 1.0,
  "clearcoat_roughness": 0.08,
  "clearcoat_transmission": 0.65,
  "clearcoat_surface_map": "wet_asphalt.png",
  "clearcoat_normal_map": "water_surface.png"
}
```

- Defaults: strength=0 with no `clearcoat_surface_map`, otherwise 1;
  roughness=0.1; transmission=1. Explicit 0 remains 0.
- The linear `clearcoat_surface_map` uses R=strength, G=roughness,
  B=transmission, each multiplied by its corresponding scalar.
- `clearcoat_transmission`=1 preserves underlying brightness apart from
  the clearcoat Fresnel effect; 0 darkens the underlying diffuse appearance
  where coat is present. The coat reflection itself is not darkened.
  Coverage-weighted attenuation avoids darkening an uncoated surface.
- Clearcoat has its own optional normal map and roughness. Initially it
  reuses the base material's reflection source, with no additional capture.
- It must be skipped when effective coat strength is zero, and not interfere
  with the base layer's authored specular defaults.
- Scalar parameters are constrained to 0..1 and must be finite.

## Planned runtime control

```c
SetMatClearcoat(material, VectorSet(strength, roughness, transmission), fadeTime);
current = GetMatClearcoat(material);
ResetMatClearcoat(material, fadeTime);
```

The setter will animate absolute material-level values, starting interrupted
fades from the current interpolated value. Get returns the current
interpolated values before pixel-map modulation. Reset animates back to
authored defaults, not a hardcoded identity vector. These verbs are **not
registered yet**.

The eventual `"reflection_mode":"shared"` will be opt-in, with one lazy,
camera-centered realtime cubemap shared across requesting materials.
`"envmap"` remains the lightweight default.
