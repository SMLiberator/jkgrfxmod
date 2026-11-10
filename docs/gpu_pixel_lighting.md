# Per-pixel dynamic lighting on GPU-cached geometry (experimental)

`gpu_pixel_lighting` selects the dynamic-lighting method for **all supported
GPU-cached world geometry and 3DOs**, not just models. It does not affect
the native JK CPU/D3D geometry path.

```json
{
  "use_model_cache": true,
  "world_cache_preview": true,
  "gpu_pixel_lighting": true
}
```

The switches are independent: enable only the caching type(s) you want.
`gpu_pixel_lighting` defaults to `false`.

| Cached geometry | `gpu_pixel_lighting=false` | `gpu_pixel_lighting=true` |
| --- | --- | --- |
| 3DO meshes | JK-style Gouraud dynamic lighting evaluated at vertices | Dynamic point lights evaluated per fragment |
| SithWorld architecture | JK native per-world-vertex dynamic light data, interpolated as Gouraud | Dynamic point lights evaluated per fragment when camera data is valid |
| Native fallback geometry | Native JK rendering | Native JK rendering |

## Shared editable pixel-light formulas

Both GPU-cached pixel-light fragment shaders include:

`assets/shaders/jk_point_lighting.glsl`

- `jk_point_sample()`: radius cutoff, attenuation, face incidence and
  nonnegative point-light contribution.
- `jk_point_compose()`: combine that contribution with the **already
  interpolated, gamma-corrected baked RGB**, including saturation.
- `JK_POINT_GAMMA`: shared gamma exponent (initially `1.8`).

`renderer/shaders.cpp` expands local GLSL includes at load time. Changes
to the shared file apply to both pixel-light paths after restarting the
renderer; no C++ recompilation is required.

The baked/static colors—including authored face lights, sector effects and
model vertex colors where applicable—are evaluated at vertices *before*
raster interpolation. Pixel lighting preserves that baked result when no
dynamic light contributes. Do not replace the baked result with
`pow(interpolated_raw_vertex_intensity, gamma)`: the nonlinear interpolation
difference causes visible bands around strongly contrasting baked vertices.

With pixel lighting disabled, 3DOs retain their separate vertex-shader
Gouraud calculation and the cached world uses the original JK world-vertex
dynamic-light buffer; changing the shared pixel formulas must not alter
those fallback modes.

## Baked and authored overbrightness

`enable_baked_overbright` defaults to `true` and controls the GPU-cached
world/3DO/sprite light-input range independently of `gpu_pixel_lighting`:

- `false`: cap each authored baked-vertex intensity, sector ambient value,
  and ExtraLight input at 1.0 (vanilla-style inputs).
- `true`: cap those inputs at 2.0. Excessive custom-level values above 2.0
  are limited; negative ExtraLight darkening remains valid.

Individual values are bounded **before** the renderer combines them.
Ambient/ExtraLight, baked light, dynamic point lights and other added sources
can together exceed 2.0; their HDR sum is not saturated to the input limit.
Per-vertex gamma/color and sector/CMP tints preserve the overbright energy,
rather than restoring it with an additional post-tint multiplier.

This affects GPU-cached geometry only. Native fallback continues to use
the native engine. Set `enable_hdr=true` for filmic tone mapping, and
optionally `enable_bloom=true` for bloom. The previous
`enable_baked_vertex_overbrightness` and `overbright_intensities` names
are accepted as deprecated configuration aliases.

```json
{
  "use_gpu_cache": true,
  "enable_hdr": true,
  "enable_baked_overbright": true
}
```

## Experimental spatial ambient from world vertices

For an initial GPU-cached 3DO lighting A/B test, enable:

~~~json
{
  "experimental_world_vertex_ambient": true,
  "experimental_world_vertex_ambient_radius": 1.0
}
~~~

The renderer captures each eligible Gouraud world-face corner's authored
vertex intensity, **per-face ExtraLight**, and the owning SithSector's
**ExtraLight floor**, using the same static formula as the cached world:

```cpp
probe = max(0.0f, authored_vertex + face_extra, sector_extra);
```

Each authored input obeys the `enable_baked_overbright` limit before
combination. The result is cached once per world-cache revision in a coarse
3D CPU grid keyed by native sector ID. Successful probe samples replace the
per-Thing **camera ambient floor**, not the model's authored static shading,
dynamic point lights, or its own ExtraLight. A sector ExtraLight of 1.0
therefore lights Things even if every surrounding world vertex is zero; the
camera/Thing ambient value itself is not sampled.

The renderer separately captures passable SithSurfaceAdjoin polygons from
the full native world table, including invisible and untextured adjoins not
present in the world render cache. When an ordinary GPU-cached 3DO is within
the configured radius of the **finite portal opening**, it samples the
neighboring sector as well. Near the opening, both sides converge to an
identical 50/50 blend; farther away each sector uses its own samples.
Solid, sky-flagged and invalid adjoins are not eligible. No direct sampling
takes place through unrelated sector walls. Crossings use one portal hop;
adjoin passability and probe lighting are captured at level-cache creation,
not tracked live after COG changes.

If the home sector lacks samples, but a valid neighboring portal has nearby
samples, the renderer can borrow those; otherwise it retains native Thing
ambient. The current version does not recompute probes on per-frame COG
vertex-light changes, and it cannot detect walls or occluders **inside** a
single sector. It is a prototype, not a complete indirect-light solver or
a source of additional emissive light for neighboring surfaces.
It excludes sprites, Camera HUDs, skyboxes, decals and native-rendered 3DOs.
The existing experimental per-fragment sector-crossing lighting takes
precedence if both toggles are enabled; disable that option for probe testing.

Start with a dark room that has bright and dark Gouraud world corners; move
a 3DO around it. Try radii from 0.5 to 2.0 world units and compare to the
toggle disabled. A neighboring sector is an important leak regression.

## Receiver-specific behavior

3DOs use local positions and normals (accounting for scaled/mirrored
transforms); their legacy non-Gouraud Diffuse mode uses the face anchor.
World surfaces use static world-space positions and authored face normals.
Their eligible Gouraud point-light samples share the same attenuation and
composition formula as 3DO Gouraud samples.

Point lights are scalar/neutral in this implementation. Per-pixel point
lights do not introduce independent point-light shadow maps. Camera HUD
Things and projected decals retain their dedicated rendering behavior;
unsupported native geometry does not receive this shader.

## Validation

Compare both caching types with the same moving dynamic light and
`gpu_pixel_lighting` toggled. Check bright and dark baked vertices,
the radius boundary, zero-light frames, face incidence, scaling, fades,
materials and performance. `tests/gpu_pixel_lighting.py` verifies source
wiring and representative formula edge cases. These tests do not replace
in-game visual parity checks.
