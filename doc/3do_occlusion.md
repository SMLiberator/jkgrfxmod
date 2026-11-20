# Experimental GPU 3DO occlusion culling

## Scope (main camera only)

`experimental_3do_occlusion_culling` is **disabled by default**. It
uses asynchronous OpenGL 3.3 `GL_ANY_SAMPLES_PASSED` queries with small
12-triangle bounding-box proxies after the *complete main opaque G-buffer*
has rendered the cached world and opaque 3DOs. It then suppresses GPU 3DO
draw-list construction for fully hidden Things in subsequent frames.

The native hook snapshots stable Thing identity and world position when
occlusion is enabled, even if sun shadows, decals and continuous ambient
are independently disabled. With this option off the extra read is absent.

This first iteration intentionally leaves native `rdModel3_Draw` / pose
callbacks, simulation, shadows, planar reflections, HUD Things, sprites,
projected decals and non-GPU native fallbacks untouched.

## Authored JK radius

JK's model-wide frustum culling uses the authored
`rdModel3.radius`, centered at `SithThing.position`, and does not grow
the sphere when mesh nodes animate outside it. The query follows this
authored Thing-centric contract, using a conservatively expanded box
(1.1 times radius plus 0.15 JK units). The radius is adjusted only for
the Thing's scale/shear. It does not rebuild or scan animated meshes.

Caveat: native frustum acceptance and occlusion acceptance are *not the
same*. A mesh animated beyond its authored Thing sphere can be seen around
an opaque wall while that sphere is occluded. This test can temporarily
hide such malformed models. Keep the opt-in disabled for parity testing;
a future implementation can exempt known out-of-bounds animations.

## Query budget and safe fallback

`gpu_3do_occlusion_query_budget`: integer 1..64, default 24. Only 3DO
Things with at least 500 cached source triangles are queried. A maximum
of 256 active native Thing identity records are maintained, with
infrequently observed records retired. Query candidates rotate and
previously occluded records are refreshed first.

Queries never block for a pending result. Two consecutive hidden results
are required before hiding anything. Results expire after four frames or
when camera position/orientation/FOV, the Thing position/scale, or world
identity changes. Query objects are deleted on world switch or teardown.
A camera inside the model sphere always draws the model. An invalid or
unavailable bound conservatively draws normally.

One limitation of asynchronous camera-local visibility: moving opaque
doors/walls can reveal an occluded Thing before its query refreshes.
The short expiration and prioritized rechecks mitigate, but cannot
guarantee a zero-frame delay. The native 3DO renderer is unaffected.

When `profile_renderer=true`, logs include:
`3DO OCCLUSION candidates`, `queried`, `query_budget`, `records`,
`submitted_total`, `completed_total`, `skipped_instance_total`.
Compare frame CPU/GPU timings and total draw calls with the feature
disabled/enabled in exactly the same scene. The number of hidden Thing
instances is not itself a performance gain if queries cost more.

## Follow-up optimization candidates

- Screen-size-driven LOD is preferable to removing all models that
  occupy only a few pixels. Start with using the engine's available
  lower-detail GeoSets; later evaluate GPU-generated simplified meshes.
- If a model's *entire conservative bound* is reliably smaller than a
  subpixel threshold, an optional distance-independent screen-size
  reject is a separate experiment, not enabled here.
- OpenGL 4.3 compute shaders / SSBOs / indirect multi-draw may support
  GPU-driven visibility and Hi-Z culling; the 3.3 code path should
  remain an available fallback.
