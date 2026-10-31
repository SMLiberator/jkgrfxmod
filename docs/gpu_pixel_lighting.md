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

When enabled, the cached GPU vertex shader still evaluates the
authored static light, ambient floor, face extra light, RGB vertex color,
colormap tint and native palette/hardware color effects **at vertices**.
Those baked colors are interpolated exactly as in the existing renderer.
Only the *point-light difference* is evaluated in the fragment shader.
It adds the difference between the dynamically lit and unlit brightness,
instead of recomputing the baked vertex lighting per pixel. This avoids
counting lights twice and, importantly, preserves the original baked color
exactly when a dynamic light is outside a given pixel's range.

Moving the complete lighting calculation to the fragment shader was
incorrect: applying the nonlinear gamma/color transform after interpolating
vertex brightness changed baked lighting on distant meshes merely because
a light appeared in JK's broad per-mesh candidate list.

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
