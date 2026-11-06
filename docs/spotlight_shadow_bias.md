# Experimental angle-adaptive spotlight receiver bias

Spotlight shadow receivers historically used a fixed **0.015 JKU** world
offset, transformed to the 24-bit perspective depth-buffer representation.
At grazing incidence (e.g. low-mounted car headlights illuminating a road)
this may cause self-shadowing / shadow acne even with a higher-resolution
shadow map.

An optional receiver bias adds **0.035 JKU at maximum grazing** when its
scale is 1.0:

```
grazing = 1 - abs(dot(geometric_receiver_normal, direction_to_light))
world_bias = 0.015 + 0.035 * scale * grazing^2
depth_bias = max(0.000025, perspective_depth_derivative * world_bias)
```

In `jkgm.json`:

```json
"enable_spotlight_shadows": true,
"spotlight_shadow_angle_bias_scale": 1.0
```

The setting accepts **0.0–2.0**. Its default is **0.0**, which keeps the
original shadow comparison for A/B testing. Suggested first experiment:
0.5, 1.0, then 2.0 if acne persists. The shadow comparison uses the
geometric normal already available for spotlight lighting, not the
material's normal map. Opaque, native translucent, and GPU 3DO translucent
receivers all use the same adaptive formula.

Increasing receiver bias can detach shadows from their casters (peter
panning), especially on nearly grazing floors. Start at low values;
compare the first visible object contact points. **No caster bias,
polygon offset, shadow filtering, shadow-map resolution, or spotlight
selection was changed.** The renderer still supports a single
shadow-casting spotlight at a time.
