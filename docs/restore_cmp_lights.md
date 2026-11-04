# Restoring CMP fullbright pixels in GPU-cached rendering

Jedi Knight's 8-bit CMP colormap contains color entries that do not change
across its 64 software lighting levels. The GPU renderer recognizes
those pixels using the *original indexed MAT source*, not RGB brightness.

## Vanilla hardware behavior by geometry mode

| Face type | Default (`restore_cmp_lights: false`) | Opt-in (`true`) |
| --- | --- | --- |
| GeoMode 4 textured MAT | Normal JK hardware lighting: CMP fullbright texels still receive shading | Indexed CMP fullbright texels stay unshaded |
| GeoMode 3 solid-color MAT | CMP fullbright palette indices remain unshaded | Unchanged: remain unshaded |

To restore software-style fullbright pixels in textures, add to `jkgm.json`:

```json
{
  "restore_cmp_lights": true
}
```

The default is **false** for vanilla JK hardware rendering parity.

This switch applies to GPU-cached 3DOs and world geometry, including their
opaque and transparent textured faces. It controls only the CMP-indexed
per-pixel fullbright mask. GeoMode 3 always uses its independent
`solid_fullbright` lookup and **does not depend on the toggle**.

Explicit emissive material maps, additive blending, albedo color,
sunlight, spotlights, dynamic lights and their normal shading are not
otherwise altered. CMP fullbright is a shading exemption, not an
additional HDR emissive light source.

With the switch off, the renderer does not build fullbright-mask textures.
Nonindexed or replacement albedo sources never get guessed CMP masks.
