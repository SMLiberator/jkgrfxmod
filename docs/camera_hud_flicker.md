# Camera Thing alpha flicker investigation

Camera-type Things (`SithThing.type == 1`, 3DO render type) are drawn
as a post-processing-exempt HUD overlay when GPU caching is active.
This follows the user-visible contract: they bypass sun/SSAO/HDR/bloom/
fog and composite before DirectDraw HUD. Camera + ThingFlag `0x20`
still uses opaque world-depth occlusion (in shader).

## Known causes and changes

- Previously Camera 3DOs entered the GPU bypass even when
  `use_gpu_cache` was `false`. They now obey the global switch.
- Native fallback is still allowed when GPU snapshot or MAT residency
  fails; that case is rendered with the regular native pipeline and
  therefore cannot remain exempt from post-processing.
- The GPU camera and mesh were deferred for presentation, but MATs
  were re-resolved from *live native pointers* one engine frame later,
  after the general GPU material cache was cleared. Changing CELs,
  transient HUD MATs or reused pointers could make individual faces
  blink. The late HUD pass now uses the material snapshots resolved
  in the **same engine frame as its geometry and camera**; it never
  dereferences a native MAT during the later draw.
- The HUD overlay has no ordinary depth test so it can appear on top
  of the scene. Approximate back-to-front sorting of translucent
  per-material draw groups can alternate overlapping HUD planes as
  the camera moves. An independent diagnostic sort switch isolates
  this mechanism without changing other scene transparency.

## Reproduction matrix (restart the game after editing jkgm.json)

All settings default to `true` and are independent of reflection/HDR flags.

| Experiment | use_gpu_cache | enable_gpu_camera_hud | camera_hud_depth_sort |
| --- | --- | --- | --- |
| GPU Camera HUD with captured materials and depth sort | true | true | true |
| Keep authored order for HUD alpha to test order instability | true | true | false |
| Force native Camera HUD, while caching other scene models | true | false | true |
| Disable the entire GPU cache, including Camera HUD | false | true | true |

**Important:** a forced native Camera Thing receives ordinary scene
post-processing and no longer has the exact GPU HUD-on-top semantics.
Do not treat it as a permanent replacement for the post-exempt GPU path.

To collect logs, use `profile_renderer: true` and
`profile_renderer_verbose: true`. Look for
`Camera HUD frame material snapshot` (instance count, resolved MAT
count and missing MAT count), and `Camera Thing HUD 3DO native fallback`.
If a frame has missing MAT snapshots, collect the 3DO/MAT names and
related material warm-up logs. If flipping `camera_hud_depth_sort`
stabilizes the image, alpha sorting of intersecting or coplanar faces
is implicated; if native-only fixes it, GPU/deferred timing is implicated.
If neither fixes the flicker, investigate the GPU-only deferred
material path. We found a concrete texture lifetime hazard: the
per-frame material snapshot previously kept only borrowed GL handles,
while a native D3D MAT Load could release an image, allowing the texture
cache to reuse and overwrite the same GL object before the deferred
Camera HUD had been drawn. Deferred HUD materials now retain the
albedo, optional transition albedo, and emissive texture-cache IDs
until the late HUD draw completes (also releasing on menus/shutdown).
This protects the actual image contents as well as the GL handles.
An in-game alpha-HUD test is still necessary to establish whether
this accounts for all observed flickering.

Switches in `jkgm.json`:

```json
{
  "use_gpu_cache": true,
  "enable_gpu_camera_hud": true,
  "camera_hud_depth_sort": true
}
```
