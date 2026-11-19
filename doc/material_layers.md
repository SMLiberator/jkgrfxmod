# Independent matcap and clearcoat material layers

**Status:** Independent MatCap rendering is implemented for GPU-cached
world and 3DO materials, and the material pack compiler supports the
metadata. Clearcoat metadata also compiles, but its shader and COG verbs
are not yet registered. The `shared` reflection mode is independent.

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
- The old `"reflection_mode":"matcap"` is rejected; use
  `matcap_map` for the independently authored layer instead.
- The RGB matcap image is imported as sRGB; its surface mask is imported
  as linear data. The mask's R controls the color contribution, and G
  multiplies the mip-based artistic blur. The layer is added as artistic
  color in the emissive/compositing path, not as a second GGX lobe.
- Existing material normal maps influence MatCap orientation, including
  materials without reflection strength. The layer does not follow
  sun/point-light specular or metallic Fresnel.
- MatCap uniforms and texture binds are skipped when inactive. The
  extra samplers use units 24/25 and are compiled out when the driver's
  `GL_MAX_TEXTURE_IMAGE_UNITS` is below 26; on such GPUs the new MatCap
  layer is unavailable while ordinary rendering continues unchanged.
- Only GPU-cached world and GPU 3DO rendering supports the new layer at
  this stage. Native triangle fallback still uses its existing shaders.

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

`"reflection_mode":"shared"` now selects one lazy camera-centered
realtime cubemap, shared across requesting cached-world and GPU 3DO
materials. It uses the existing six-view capture, same interval and
resolution, and is not subject to the persistent CaptureEnvMap Cog-only
static scenery policy. `"envmap"` remains the lightweight default.
