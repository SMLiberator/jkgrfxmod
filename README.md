# JkGRfxMod

JkGRfxMod extends **JkGfxMod** for *Star Wars Jedi Knight: Dark Forces II* and JkGR. This documentation covers **features added or changed by JkGRfxMod**; the original JkGfxMod documentation remains the reference for unchanged functionality.

JkGRfxMod uses the patched JK23/JKGM executable and a matching 32-bit `renderer.dll`. Some native engine/COG extensions require the corresponding `JK.dll`. Executables are **not** distributed by this repository.

## Installation

See [installation and executable requirements](doc/install.md). In brief:

1. Copy `renderer.dll`, `jkgm.json`, and the `jkgm/` shader and material assets from the same build to the game folder.
2. Generate `jkgm.exe` from a supported `jk23.exe` using `patch_jkgm_bootstrap.py` (see the installation guide).
3. Run `jkgm.exe` directly, using the ordinary JK command-line arguments.

The GPU cache is enabled by default. Many advanced rendering options, such as extra shadows, per-Thing reflection probes, and experimental optimizations, have their own configuration switches.

## Rendering additions

| Area | Added or changed behavior |
| --- | --- |
| GPU geometry | Indexed GPU caching for world surfaces, 3DOs, ordinary sprites and supported procedural geometry; live MAT animation, authored lighting and material state are preserved. |
| Materials | Normal maps, dual independently scrolling normal layers, surface maps, parallax, MatCaps, clearcoat, emissive animation, dithered alpha, blend modes and Fluid absorption. |
| Reflections | Camera-centered Shared cubemap, dedicated Cube and dual-paraboloid Hemi probes, world-surface Planar reflections, screen-space reflections, and COG-triggered EnvMap capture. |
| Lighting | COG-controlled directional sun, optional shadow cascades and moving-Thing shadows, textured spotlights, colored Thing lights, sunlight caustics and GPU pixel lighting. |
| Visibility and performance | GPU Thing occlusion queries, authored/automatic 3DO LOD support, optional instancing, CPU visibility workers, packed GPU vertices, and optimized postprocessing passes. |
| HUD and effects | Postprocessing-exempt Camera Things, adjustable HUD ammo display, fog modes, SSAO, HDR controls, film-style effects and per-Thing sprite opacity/roll. |
| Engine extensions | Additional JK23 COG verbs, native Thing/node attachments, precise raycasts, sprite variants and optional mesh collision handling. |

## Material pack additions

Material packs continue to use JkGfxMod's replacement workflow. JkGRfxMod extends the material definitions with the following properties:

| Feature | Material properties |
| --- | --- |
| Surface detail | `normal_map`, `normal_map2`, `normal_strength`, `normal_strength2`, `normal_scroll`, `normal_scroll2`, `surface_map`, `displacement_map` |
| Albedo and emission | `albedo_scroll`, `emissive_scroll`, `emissive_pulse`, `use_original_albedo` |
| Reflections | `reflection_mode`, `reflection_strength`, `roughness`, `metalness` |
| MatCap | `matcap_map`, `matcap_factor`, `matcap_roughness`, `matcap_surface_map`, `matcap_lighting_influence` |
| Clearcoat | `clearcoat_strength`, `clearcoat_roughness`, `clearcoat_transmission`, `clearcoat_surface_map`, `clearcoat_normal_map` |
| Transparency | `alpha_mode`, `blend_mode`, `fluid_thickness`, `depth_bias` |

`reflection_mode` supports `envmap`, `screen`, `planar`, `hemi`, `cube` and `shared`. **Shared** uses a camera-centered six-face cubemap. **Hemi** is a separate, Thing-local two-disk capture. **Planar** captures reflections for eligible world surfaces.

`depth_bias` changes polygon depth testing, not world-space geometry; positive values favor the material toward the camera. The same bias is used by native and GPU-cached triangles.

Use `albedo_scroll` and `emissive_scroll` for UV motion; `emissive_pulse` supplies sine, square, flicker or random intensity animation. A MatCap is independent of the reflection mode and can be configured to respond to scene lighting.

See [material layer details](doc/material_layers.md), [material compiler documentation](doc/compile.md) and [offscreen reflection/capture documentation](doc/offscreen_captures.md).

## COG extensions

The patched game and associated JK.dll expose additional controls. The table below highlights the JkGRfxMod additions; it is not a replacement for the original Jedi Knight COG reference.

| Purpose | COG API |
| --- | --- |
| Sunlight | `SetSunInfo`, `GetSunDirection`, `GetSunColor`, `GetSunIntensity`, `ResetSun` |
| Spotlights | `SetSpotlight`, `ClearSpotlight`, `SetSpotlightIntensities` |
| Lights | `SetThingLightRGB`, `GetThingLightRGB`, `SetSaberLightIntensity` |
| Material control | `SetMaterialColor`, `SetMaterialAlbedo`, `SetMatEnvMap`, `SetMatClearcoat`, `ResetMatClearcoat`, `ReloadMaterial` |
| Photography and environment | `CameraToMat(cameraThing, targetMat, FOV)`, `CaptureEnvMap(sourceThing, fadeTime)` |
| Thing/sprite appearance | `SetThingOpacity`, `SetSpriteInfo`, `GetSpriteInfo`, `SetSpriteRoll`, `GetSpriteRoll` |
| Attachments and animation | `AttachThingToThingNode`, `DetachAttachedThings`, `GetThingAttachThing`, `GetThingJointNode` |
| Spatial queries | `RayCastEx`, `GetThingCameraDist`, `GetThingCameraDistSq`, `GetThingOffsetPos`, `GetThingPosOffset` |
| AI queries | `AIGetFireTarget`, `AIGetMovePos`, `AIGetMoveTargetPos`, `AIGetLookDir` |
| Numeric helpers | `Min`, `Max`, `Clamp`, `Sign`, `Lerp`, `InverseLerp` |
| HUD | `SetAmmoBin`, `GetAmmoBin` |

`CameraToMat` is a **one-shot capture**. It replaces only the selected MAT's albedo image and retains its factors, emission, normals and other layers. `FOV>0` uses perspective degrees, `FOV<0` uses its absolute value as the orthographic vertical span in JK units, and `FOV=0` leaves the current photo unchanged. A script can call the verb repeatedly for a changing image. `ReloadMaterial` clears the target MAT's runtime overrides and restores its authored properties.

`RayCastEx(sourceThing, maxDistance)` performs an explicit, precise query along the source Thing's Look vector. On a hit, the standard `GetRayHitPos`, `GetRayHitNormal`, `GetRayHitDistance`, `GetRayHitThing` and `GetRayHitSurface` accessors report the result.

`SetAmmoBin(bin)` overrides the local player's HUD ammo source; `SetAmmoBin(-1)` displays the no-ammo marker. It does not change weapon inventory or consumption.

The existing fog COG API accepts `SetFogInfo(vector(start, end, opacity), fadeTime)`; an opacity of zero disables fog.

## Configuration and feature guides

- [JkGRfxMod configuration changes](doc/config.md) — added/changed JSON settings, valid ranges and defaults.
- [Installation](doc/install.md) — executable patching, packaging and requirements.
- [Material compiler](doc/compile.md) and [MatCap/clearcoat](doc/material_layers.md).
- [Reflection captures](doc/offscreen_captures.md) and [planar reflections](doc/planar_reflections.md).
- [3DO occlusion](doc/3do_occlusion.md) and [GPU 3DO submission/cache](doc/3do_cpu_submission.md).
- [Sun caustics](doc/sun_caustics.md) and [Thing opacity](doc/thing_opacity.md).
- [Gameplay and native extensions](doc/native_gameplay_extensions.md).
- [Additional COG details](doc/cog_symbol_extensions.md), [camera axes](doc/cog_camera_axes.md), [camera distance](doc/cog_camera_distance.md), [joint nodes](doc/cog_thing_joint_node.md), [Thing creation](doc/cog_create_thing.md) and [native parent attachment](doc/native_spawn_parent.md).

## Building and verification

The renderer and tools build using the solution `jkgfxmod.sln` (Windows x86, Visual Studio 2022). CI builds the actual binaries and runs behavioral unit tests for geometry, material handling, engine integration and patching. The release workflow produces artifacts without redistributing the original game's executable.
