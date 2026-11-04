# Experimental normal-mapped diffuse lighting

Opt-in JSON option:

```json
{
  "enable_normal_mapped_diffuse": true
}
```

The default is `false`: existing JK GPU-cached appearance is preserved.

## Coverage

- GPU-cached 3DOs and SithWorld: normal maps affect **added** opaque
  projector/spotlight and direct sun diffuse illumination through the
  existing G-buffer normal, without changing the native baked/Gouraud
  sector lighting or the original geometry-based shadow maps.
- GPU-cached 3DOs also use those normals for existing forward-transparent
  sunlight and projected spotlights.
- GPU-cached world and 3DOs use normal maps in per-fragment dynamic point
  diffuse lighting when `gpu_pixel_lighting` is also enabled. Gouraud
  point lighting stays unchanged while that option is off. For 3DOs,
  LightMode 2 preserves flat incidence; LightMode 3 gains normal detail.
- Normal mapping is independent of `enable_reflections`, any specular
  strength, and whether a material has an EnvMap. Matte materials with
  replacement normal maps are eligible.

## Boundaries

- No authored emission changes, no alteration to `restore_cmp_lights`,
  and no normal mapping for GeoMode 3 solid colors, HUDs, decals,
  sprites, fullbright pixels or ThingFlag 0x20 lighting-exempt models.
- Fluid materials deliberately use geometric normals for diffuse light,
  even with this option enabled. Fluid normal maps still shape environment
  reflections and specular highlights; ripples should not make liquid appear
  thick or sculpted by a flashlight.
- Shadow *silhouettes* and occluders are still geometry/depth-map based:
  normal maps only change the receiver's light incidence. They do not
  create micro-geometry shadow casters or self-shadowing.
- Opaque G-buffer normals are shared by the sunlight/spotlight compositor
  and SSAO. Consequently this experimental switch also lets SSAO use
  the perturbed receiver normals; adding an independent geometric
  normal channel would be required to avoid that interaction.
- The existing GPU-cached world forward-alpha path does not receive
  projected spotlights or diffuse sun, so this switch cannot add them
  there; its per-fragment point diffuse lighting can use mapped normals.
- Native fallback rendering has not been modified by this experiment.
