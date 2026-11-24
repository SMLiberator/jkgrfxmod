# Planar reflections

`reflection_mode: "planar"` gives supported **GPU-cached world surfaces** a mirrored scene reflection. It is intended for flat water, mirrors and other planar materials.

Planar captures require `enable_reflections`, `enable_reflection_capture` and `enable_reflection_probes`. When dedicated probes are disabled or a capture is unavailable, the material uses its available EnvMap fallback.

## Capture and receiver behavior

The renderer selects an eligible visible world plane and constructs an independent camera reflected across that plane. It captures cached world surfaces, sky apertures and 3DO Things that are visible from the reflected camera, including eligible Things outside the main camera's view. Reflective Thing acquisition, portal traversal and frustum culling use the mirrored camera rather than the gameplay camera.

The mirrored render target uses an HDR color image and depth buffer. Capture geometry is clipped against the reflecting plane, with corrected winding for the mirrored view. Material-specific color, normal maps, roughness, reflection strength and Fluid properties are applied when the world surface samples the completed image.

`reflection_capture_resolution` determines the **shorter side** of the planar render target. The other dimension follows the gameplay camera's aspect ratio. Reflective captures are reused when their camera/plane state permits it; an asynchronous receiver-visibility query can skip capture work for an occluded plane.

Normal maps can perturb the sampled reflection, with the material's roughness affecting the blur. Fog is applied consistently to avoid double fogging. Uncovered capture pixels may fall back to an available authored or generated EnvMap.

## Scope and configuration

- Only eligible GPU-cached **world surfaces** use Planar as a receiver. Native fallback and 3DO receiver materials use their applicable fallback.
- Captures have independent camera visibility and include sky and eligible animated 3DOs.
- The capture omits unnecessary secondary postprocessing, shadow-map rendering and nested reflection capture.
- Multiple independently updating mirror planes are not part of the current capture path.
- Planar captures do not use the dedicated Hemi/Cube Thing-distance limit.

Example material definition:

```json
{
  "name": "mirror_water",
  "reflection_mode": "planar",
  "reflection_strength": 1.0,
  "roughness": 0.08,
  "normal_map": "ripples.png",
  "replaces": ["water.mat"]
}
```

See [reflection capture controls](offscreen_captures.md) and [configuration changes](config.md).
