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
| 16–18 | Sun near/far/dynamic shadow | Sun near/far/dynamic shadow (direct specular include) | Sun near/far/dynamic shadow | Unused |
| 19 | **Sector-crossing samplerBuffer** | Unused | Unused | Unused |
| 20 | **Reserved for future forward sky-aperture sampler2DArray** | Unused | Unused | Unused |

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
Forward units 0–19 are reserved already; a future sky-aperture array on
unit 20 needs **at least 21** available fragment texture units or a
GLSL variant that compiles that sampler out on less capable hardware.

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
