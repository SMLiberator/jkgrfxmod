# Experimental sun caustics: Fluid transmission

Enable with **"experimental_sun_caustics": true** in jkgm.json.
Default is \`false\`. Requires both \`enable_sun\` and
\`enable_sun_shadows\`, a sky-enabled world, and sufficient fragment
texture units (at least 20). There are **no new material fields**.

- Sources are **Fluid-blend SithWorld surfaces**, including passable
  adjoins, and **GPU-cached 3DO Fluid faces**. Sprites, projected decals,
  HUD/camera models and procedural particle meshes never generate caustics.
- The first Fluid intersection along each sunlight ray writes depth and
  its material's existing \`fluid_thickness\` coefficient. The ordinary
  opaque sun shadow map is unaffected. Two 512×512 layers match the near
  and far sun cascade projections.
- A deterministic, tileable 256×256 cellular-ridge texture is generated
  **once per GL context**, with GPU mipmaps generated once. Animation
  samples it twice in light-space, without re-generating textures or
  redrawing stationary Fluid geometry solely because time changed.
- Fluid density/thickness and sun-ray travel distance soften the pattern
  using mip selection (\`textureLod\`) and attenuate intensity exponentially.
  Five global `sun_caustics_*` controls adjust appearance with
  no new material fields:

  | JSON key | Default | Range | Effect |
  | --- | ---: | ---: | --- |
  | `sun_caustics_intensity` | `3.0` | `0..16` | Overall brightness |
  | `sun_caustics_scale` | `3.0` | `0.25..16` | Higher means finer/smaller patterns |
  | `sun_caustics_spread` | `1.0` | `0..8` | Density/distance mipmap softness |
  | `sun_caustics_attenuation` | `1.0` | `0..8` | Density/distance fading |
  | `sun_caustics_speed` | `1.0` | `0..8` | Animation rate; 0 freezes movement |

  For a strong first test try intensity `5.0`, scale `4.0`,
  spread `0.5` and attenuation `0.5`. These controls do not
  regenerate the procedural texture or the Fluid shadow layers.
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
