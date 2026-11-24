# Reflection and camera captures

JkGRfxMod supports realtime reflection captures and two one-shot COG image captures. All capture cameras use independent world/3DO geometry, without changing the active Jedi Knight camera.

## Realtime reflection modes

| Material `reflection_mode` | Capture |
| --- | --- |
| `shared` | Camera-centered, six-face cubemap reused by eligible world and 3DO materials. |
| `cube` | Six-face cubemap centered on a reflective Thing. |
| `hemi` | Two +Z/-Z dual-paraboloid disks centered on a reflective Thing, with overlapping horizon coverage. |
| `planar` | Mirrored perspective capture for eligible world surfaces such as flat water and mirrors. |
| `screen` | Screen-space reflections from the completed opaque frame, when enabled. |
| `envmap` | Authored or COG-controlled environment map; no realtime probe required. |

`enable_reflection_capture` enables realtime captures. `enable_reflection_probes` enables dedicated Hemi/Cube and Planar captures; when disabled, eligible materials use the Shared cubemap. `enable_reflections` remains the master reflection switch. `CaptureEnvMap` is an independent on-demand command.

`reflection_capture_resolution` sets the Cube/Shared face width and height, the baseline Hemi resolution, and the shorter edge of Planar captures. Each Hemi disk is 2× the baseline linear resolution. `reflection_capture_update_interval` sets the minimum seconds between complete realtime probe updates; a zero value updates as often as the renderer allows. Previously completed images remain available between refreshes.

Dedicated Hemi/Cube probes can limit the distance of reflected Things using `reflection_probe_max_distance`; world geometry and sky are not range-limited. Shared preserves world geometry, sky and LEVELGEO Things at full distance. Probe owners and eligible attached assemblies are excluded from their own local captures.

Secondary cameras use independent portal/frustum visibility, native Thing pose acquisition and JK sky-aperture rendering. If a direction has no captured geometry, material shaders can retain the EnvMap fallback. Camera HUD elements and unrelated presentation/postprocessing passes do not appear in the captures.

See [planar reflections](planar_reflections.md) for surface selection and receiver details, and [configuration changes](config.md) for capture settings.

## `CaptureEnvMap(sourceThing, fadeTime)`

This COG command takes one environment photograph from a Thing's world-space position. It captures six 90-degree views, converts them to an HDR equirectangular texture, then makes the result available as the global EnvMap.

- `envmap_capture_face_resolution` sets the size of each cube face. An `N×N` capture yields a `4N×2N` global image.
- `fadeTime` is measured in seconds after the complete image becomes available; zero replaces the global map immediately.
- A new request during an ongoing fade continues from the currently displayed blend, without a black frame.
- Authored per-MAT environment maps and material strength, roughness and metalness are preserved. Explicit MAT overrides retain their priority.
- Capture requests are one-shot and use eligible static scene geometry, visible sky and the source position copied at request time.
- `SetGlobalEnvMap` or `ResetGlobalEnvMap` replaces the generated global override.

Example:

```c
CaptureEnvMap(player, 1.5);
```

## `CameraToMat(cameraThing, targetMat, FOV)`

`CameraToMat` captures a single image from a camera Thing and replaces **only the selected MAT's albedo image**. The source camera is sampled when the queued capture executes. The image remains frozen until another request or a material reset.

- Positive `FOV` values give the horizontal perspective field of view in degrees (less than 179).
- Negative `FOV` values select orthographic projection: their absolute value is the **vertical coverage in JK world units**. For example, `-0.1` covers 0.1 JKU vertically, `-1` covers 1 JKU, and `-20` covers 20 JKU. Horizontal coverage follows the target MAT's aspect ratio.
- `FOV=0` is a **no-op**: no new photograph is queued, and the last completed photo (or pending request) is preserved. Use `ReloadMaterial` to discard an existing photograph. Moving the source camera farther away does not enlarge orthographic coverage.
- Capture resolution follows the target MAT's original resident dimensions and aspect ratio.
- Albedo factor and color tint, normal and surface maps, emissive properties, clearcoat, reflectivity and UV behavior are unchanged.
- At most four photographed material images are retained. Capture targets are released on world changes.
- Capture sources exclude previously generated CameraToMat images to prevent feedback.
- A COG timer can issue repeated one-shot requests to create a live camera display.
- `ReloadMaterial(targetMat)` (alias `ResetMaterial`) discards the image and restores authored material defaults, including per-MAT runtime overrides.

Example:

```c
CameraToMat(cameraThing, screenMat, 75);  // perspective photo, 75 degrees
CameraToMat(cameraThing, screenMat, -0.5); // orthographic, 0.5 JKU tall
CameraToMat(cameraThing, screenMat, -20);  // orthographic, 20 JKU tall
CameraToMat(cameraThing, screenMat, 0);    // no new capture
ReloadMaterial(screenMat);                 // restore authored material
```

Perspective captures include world sky apertures. They are scene captures, not a full replay of transparent sprites, particles, effects and postprocessing.
