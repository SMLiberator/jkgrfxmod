# OpenGL texture unit registry

The authoritative sampler inventory is [`renderer/texture_units.hpp`](../renderer/texture_units.hpp).
The `tests/texture_unit_registry.py` CI check expands GLSL `#include` files,
requires all scene shader sampler uniforms to be registered, and checks
sampler types and exclusive aliases. Each shader program has its **own**
allocation. Units can be reused in unrelated fullscreen, material or
postprocessing passes, but incompatible active sampler types **cannot**
reference the same unit within a program.

| Unit | GPU 3DO | GPU world | Native opaque/translucent | Opaque composite |
|---|---|---|---|---|
| 0 | Albedo | Albedo | Albedo | Color |
| 1 | Emissive | Emissive | Emissive | Emissive |
| 2 | Surface | Surface | Displacement | AO |
| 3 | Normal | Normal | Surface | Depth |
| 4 | EnvMap | EnvMap | EnvMap | Unused |
| 5 | Normal 2 | Normal 2 | Translucent scene depth | Scene normal |
| 6 | Albedo 2 | Albedo 2 | Normal | Spotlight 0 |
| 7 | EnvMap 2 | EnvMap 2 | Normal 2 | Spotlight 1 |
| 8 | Scene depth | Scene depth | Albedo 2 | Spotlight 2 |
| 9 | Displacement / decal receiver light | Unused | EnvMap 2 | Spotlight 3 |
| 10 | Spotlight shadow (depth compare) | Unused | Translucent spotlight shadow | Spotlight shadow |
| 11 | Fullbright palette / decal AO | Fullbright palette | Unused | Unused |
| 12–15 | Spotlight cookies 0–3 | Unused | Translucent spotlight cookies 0–3 | 13–15 sun shadows |
| 16–18 | Sun near/far/dynamic shadow | Sun near/far/dynamic shadow (direct specular include) | Sun near/far/dynamic shadow | 16 sky aperture; 17–18 unused |
| 19 | **Sector-crossing samplerBuffer** | Unused | Unused | Unused |
| 20 | **Opt-in sky-aperture sampler2DArray** | **Opt-in sky-aperture sampler2DArray** | **Opt-in sky-aperture sampler2DArray** | Unused |

The two GPU 3DO same-type aliases are deliberately restricted to
*mutually exclusive* decal and ordinary material draws:

- Unit 9 uses the displacement map for ordinary materials or the receiver
  light from the opaque emissive buffer for **decal-only** rendering.
  Projected decals do not use parallax.
- Unit 11 uses the material fullbright mask or the decal-only receiver AO.
  Both are `sampler2D`.

**Before adding a sampler**, register it under every program that declares
it (including shared GLSL), confirm its type and unit, and run CI. All
sampler uniforms start at unit 0 in OpenGL: an early return because a
feature is disabled must **not** skip assignment of a distinct
`samplerBuffer`, `sampler2DShadow`, or `sampler2DArray` unit. The
hardware's `GL_MAX_TEXTURE_IMAGE_UNITS` limit must also be respected.
Forward units 0–19 are already occupied; the experimental sky-aperture
array on unit 20 requires **at least 21** available fragment texture units.
On less capable hardware, the GLSL variant compiles that sampler out.

Other independent passes reuse low slots: SSAO 0 normal, 1 depth,
2 noise; presentation 0 source, 1–5 bloom; dynamic VSM prefilter 0 depth;
menu 0 texture. The main registry covers the five most complex
simultaneous-sampler scene programs. The CI check protects their complete
GLSL inventories but does not replace a real OpenGL runtime test.

At link time, `initialize_registered_samplers` assigns registered sampler
locations to their units **before any frame draws**, including inactive
shadow/buffer samplers. It restores the previously bound GL program and
emits a warning if an active sampler requires a unit not supported by the
current GPU. Source CI verifies this initializer is present. Unavailable
high slots still need a future compile-time feature variant to operate on
minimum-capability GL3 drivers; the warning is not a fallback renderer.

## Experimental sky-only sunlight aperture

The opt-in `experimental_sun_sky_aperture` reuses the sunlight cascades
but rasterizes sky-flagged SithWorld surfaces separately. The opaque
composite samples a two-layer `sampler2DArray` at **unit 16**;
native forward and GPU-cached 3DO programs sample it at **unit 20**.
Unit 19 is still the sector-crossing `samplerBuffer`, not a spare unit.

`JKGM_SUN_SKY_APERTURE` is defined only when
`GL_MAX_TEXTURE_IMAGE_UNITS >= 21`. Drivers with fewer fragment
units compile out the sampler and keep previous sunlight behavior;
they never bind invalid array units. Resources are created only when
the config is enabled and hardware meets that limit.

GPU-cached SithWorld direct sunlight specular highlights are gated by the
same sky aperture through `world_cache_direct_specular.glsl` (forward
unit 20). Its authored ambient, non-sun environment reflections, and
point-light specular contributions remain unchanged.

## Sky-aperture coverage expansion

Instead of four per-fragment aperture-depth samples and visibility filtering,
the aperture map now uses one nearest depth sample per receiver, followed by
a binary depth test. The cached sky geometry is rendered once at its original
light-space coordinates plus four diagonal offsets of **0.75 ordinary
sun-shadow texel** in each axis. The five versions are submitted with
`glMultiDrawElements` in five calls, but only when the aperture map's
projection/sky-surface signature changes; the aperture depth is cleared
only once. This small silhouette expansion aims to bury the hard sky-mask
edge within the existing PCF/VSM shadow, avoiding additional per-pixel
filtering and its jagged standalone mask boundary. It may reintroduce
some light leaks if the normal sun-shadow map does not represent a nearby
wall; compare against the earlier filtered build if that occurs.

Depth textures remain `GL_NEAREST` and their precision, sampler allocations,
and existing sun-shadow filters are unchanged.

## World-cache sunlight: VSM sampler compatibility

The cached-world direct GGX shader now compiles with `JKGM_SUN_VSM` when
`experimental_sun_shadow_vsm` and `enable_sun_shadows` are enabled,
just like the native and GPU-3DO sunlight shaders. Units 16/17 then use
`sampler2D` to sample RG32F near/far moments and apply the same VSM
Chebyshev formula and light-bleed parameters. With VSM disabled they use
`sampler2DShadow` against the depth maps. The dynamic Thing atlas
remains a `sampler2DShadow` on unit 18 in both modes.

Previously cached-world direct specular sampling read VSM moment color
textures through a depth-comparison sampler, a format/type mismatch
that could cause undefined visible highlights. This change does not
modify VSM prefiltering, base sunlight diffuse, or sky apertures.

## Sun-shadow diagnostic

`sun_shadow_debug_view` 1..4 visually compares original depth
visibility, filtered PCF/VSM static cascade visibility, sky access, and
their product, respectively; default is zero (off). It is an
opaque-composite diagnostic and does not include the dynamic Thing
atlas or later translucent rendering. Original depth textures are
bound to additional compositor units **17/18**, while the selected
PCF/VSM textures remain on **13/14** and sky aperture remains on **16**.
In VSM mode, raw depth is sampled using `sampler2D` with manual
receiver comparison because the VSM prefilter disables depth-compare
mode on the original textures; in PCF mode the diagnostic uses a
`sampler2DShadow`. Both variants are registered in the C++ sampler
registry. Diagnostics compile out on devices with fewer than 21
fragment texture units and do not change regular shading.

## Optional VSM occlusion guard

`experimental_sun_vsm_depth_guard` adds four comparisons against
the original near/far depth map in the opaque sun-lighting composite,
then caps VSM visibility by their bilinearly interpolated result. It
reuses existing diagnostic bindings **17/18**, while VSM moment maps
remain at **13/14**. It is default-off and requires 21 fragment units
and the VSM shader variant. This can suppress VSM-only false-lit strips
under roof casters, at the cost of extra depth reads and potentially
narrower penumbras. Aperture capture, shadow bias, VSM blur, PCF, and
translucent paths are unchanged.
