# Per-Thing 3DO opacity fades

Supported by GPU-cached `rdModel3` Things.

```c
SetThingOpacity(thing, opacity, dither, fadeTime);
float current = GetThingOpacity(thing);
```

- `opacity` is clamped by validation to **0..1** (out-of-range values are rejected).
- `dither >= 1` uses GPU opaque-pass screen-door dithering for ordinary
  normal-blend materials; smaller values use sorted alpha blending.
  Special additive/multiply/fluid/emission materials retain their
  existing blend modes, with the additional Thing-opacity multiplier.
- `fadeTime` is in seconds (0..3600); zero applies immediately.
  Interpolation is linear in wall-clock milliseconds.
- `GetThingOpacity` returns the *current interpolated value*, not target.
  Invalid or unconfigured model Things return 1.0.
- Retargeting an active fade begins at its present opacity. The dither
  selection changes immediately, not progressively.
- The state belongs to the **SithThing pointer plus native signature**.
  It never changes or clones the shared model, MAT, mesh or GPU resource.
  Recycled native Thing slots do not inherit a previous owner's fade.

### Rendering and shadows

Ordinary 3DOs at full opacity retain the original pass routing. A partially
faded Thing selects the alpha pass or the opaque dither pass; zero opacity
skips visible GPU draws. The independent emitter output fades too.

**All 3DO shadow capture paths**—spotlights, dynamic sun atlas and sun
cascades—interpret partial material and Thing alpha as *dithered depth
coverage* regardless of visible transparency selection. Existing alpha-blended
3DO materials may cast partial shadows instead of being excluded solely for
opacity. Masked materials first honor their authored cutoff. Additive,
multiply, other special effects and emission-only materials remain excluded.

The dither is a fixed Bayer screen-door pattern in shadow texture pixels,
with no randomized temporal pattern. Shadow-cache signatures include live
Thing opacity and material opacity; fading casters invalidate their own
cached coverage, without invalidating their source geometry.

### Known restrictions

- The system is a **GPU-cached 3DO rendering feature**; it cannot make the
  unmodified native Direct3D fallback renderer display partial alpha fades.
  Verify material residency/fallback diagnostics for test models.
- Original translucent multi-mesh 3DOs can still exhibit painter-order
  artifacts when rendered with true alpha blending. The dither option
  avoids many of these issues.
- Main-camera and planar-reflection 3DOs both honor the opacity. The
  secondary capture composites alpha-faded model groups back-to-front after
  opaque reflected geometry and performs dithered fades in the depth-writing
  opaque pass. Unrelated pre-existing special/Fluid effects retain the planar
  capture's simplified material exclusions.
- Fully transparent **special** materials, like emissive-only VFX, keep
  their original no-shadow policy.


