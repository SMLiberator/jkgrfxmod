# GPU-cached 3DO per-pixel dynamic lights (experimental)

This first phase is **GPU-cached models only**. It does not modify
the original JK CPU/D3D renderer or native SithWorld architecture.

Enable in jkgm.json:

```json
"gpu_pixel_lighting": true
```

The switch defaults to `false` for A/B comparison. With `use_model_cache`
disabled it has no effect.

## Design

JK's per-Thing camera light list is captured when each cached mesh is
submitted. The native list supplies positions, range, scalar intensity,
and attenuation, with the existing conservative mesh-level selection.
It remains limited to 16 affecting lights per mesh in this first phase;
an overflow retains the existing native rendering and separate sun-depth
caster fallback. This does not add point-light shadows.

When enabled, the cached GPU vertex shader passes raw local geometry and
original JK light fields to the fragment shader instead of applying
native point lights to its Gouraud color. The fragment shader reconstructs
the authored static light, ambient floor, face extra light, RGB vertex color,
colormap tint, and native palette/hardware color effects after evaluating
point-light falloff at the fragment position. This avoids counting the
same dynamic light both at the vertex and the fragment.

Lighting modes retain JK semantics: FullyLit/NotLit do not use the
point lights, Diffuse uses the face anchor and face normal, and Gouraud
uses interpolated local position and normal. Only the latter produces
smooth point-light gradients across large faces by design.
Scaled/mirrored node transforms use the existing inverse-transpose
normal and affine-aware distance path.

Sith Camera HUD Things and GPU-projected decal geometry retain their
existing lighting behavior. Native fallback meshes and the world do
not receive the extra pixel shading.

## Next phase: emissive light proxies

Automatic emissive-to-point-light proxies are **not implemented in this
first phase**. The intended source is the currently resolved MAT emissive
factor, multiplied by a bounded brightness-weighted approximation of its
emissive image. Compute this at texture upload (not by GPU readback every
frame), place a limited number of lights on emissive mesh groups, and
cull by receiver position/radius before transferring lights to the
fragment shader. This should be a distinct A/B switch and avoid affecting
the original native dynamic light list or JK's 16-light overflow policy.

No automatic point-light shadows or indirect bounce lighting are
planned for the initial approximation.

## Testing

Compare the same large, low-poly 3DO mesh with the switch off/on,
with a nearby moving native point light. Inspect lighting gradients,
Gouraud/flat Diffuse modes, RGB vertex colors, mirrored/scaled Things,
alpha-blended faces, Camera HUDs, native/world geometry and performance.
The repo's Python regression checks the shader-stage interfaces and
the no-double-lighting configuration, not the runtime visual result.
