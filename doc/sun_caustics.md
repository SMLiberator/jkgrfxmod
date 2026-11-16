# Experimental sun caustics: Fluid transmission

Enable with **"experimental_sun_caustics": true** in jkgm.json.
Default is \`false\`. Requires both \`enable_sun\` and
\`enable_sun_shadows\`, a sky-enabled world, and sufficient fragment
texture units (at least 20). There are **no new material fields**.

- Sources are **Fluid-blend SithWorld surfaces**, including passable
  adjoins, and **GPU-cached 3DO Fluid faces**. Sprites, projected decals,
  HUD/camera models and procedural particle meshes never generate caustics.
- The first Fluid intersection along each sunlight ray writes depth,
  the material's existing `fluid_thickness` coefficient, and water albedo
  RGB represented by the final 1×1 mip of its albedo texture,
  multiplied by the material albedo factor. If no texture is present, the
  factor alone provides the tint. Material texture transitions blend the
  two representative colors. No surface-UV sampling is necessary and
  UV scrolling alone no longer invalidates the Fluid transmission capture.
  A MAT cel/texture, color-factor or transition change still invalidates it.
  This reuses the existing transmission sampler, replacing the R16F
  thickness map with RGBA16F (R=thickness, GBA=material-average RGB).
  The ordinary opaque sun shadow map is unaffected. Two 512×512 layers
  match the near and far sun cascade projections.
- A deterministic, tileable 256×256 cellular-ridge texture is generated
  **once per GL context**, with GPU mipmaps generated once. Animation
  samples it twice in light-space, without re-generating textures or
  redrawing stationary Fluid geometry solely because time changed.
- Fluid density/thickness and sun-ray travel distance soften the pattern
  using mip selection (\`textureLod\`) and attenuate intensity exponentially.
  The visual preset is authored directly in
  `assets/shaders/post_opaque_composite.frag`. Edit its intensity,
  pattern projection, animation offsets, spread and attenuation to tune
  the appearance; these numerical settings are deliberately not in JSON.
  `jkgm.json` retains only the `experimental_sun_caustics` enable flag.
  These changes do not regenerate the procedural texture or Fluid layers.
- On **opaque** world/3DO receivers, caustics transition smoothly over
  `CAUSTICS_SHORE_FADE_DISTANCE` of sunlight-ray water depth rather than
  disappearing at a fixed depth bias. Near/far Fluid transmission maps blend
  over the same camera-depth range as regular sun shadows.
- Ordinary direct sunlight also fades through Fluid with the separately
  shader-editable `SUN_WATER_DIRECT_ATTENUATION` coefficient (default
  `1.0`). This attenuation is intentionally slower than caustic falloff;
  normal sunlight is unchanged above water, when no Fluid is captured, or
  when the experiment is disabled. A new shader-only constant,
  `SUN_WATER_TINT_STRENGTH`, controls selective RGB absorption: water
  with lower red albedo absorbs more red sunlight; white Fluid albedo keeps
  the previous neutral attenuation. Use `0.0` for old grayscale sunlight.
  Caustics inherit the relative hue but retain the tuned overall brightness.
  This doesn't change underwater ambient light or Fluid camera-ray absorption.
- The transmission depth/density maps still use nearest sampling; raster
  precision at water polygon edges and the water surface's own shoreline
  compositing are independent from this lighting correction.
- On **opaque** world/3DO receivers, caustics obey the ordinary sun
  visibility, dynamic caster shadows, sky aperture and distance fog.
  The visible receiver must lie farther from the sun than the Fluid
  interface along the same ray. This supports underwater object shadows.
- Multiple overlapping Fluid layers deliberately choose the **nearest
  one to the sun**. The first experiment does not add translucent
  receiver lighting, refraction-based ray focusing, or normal-map-driven
  caustics. The pattern is a procedural visual approximation.

When the option is off, no additional transmission FBOs or procedural
textures are allocated and the compositor's caustics branch is disabled.
The existing Fluid absorption and reflections remain unchanged.

For A/B testing, compare the same sunlit water location with the flag
on/off. Use \`profile_renderer\` to inspect CPU/GPU effects and monitor
whether fluid world/3DO changes force frequent transmission re-captures.
