# JkGRfxMod configuration additions and changes

This page covers configuration options added or changed by **JkGRfxMod** relative to JkGfxMod. For unchanged display, antialiasing and window settings, use the original JkGfxMod configuration reference. Defaults here are taken from `common/config.hpp`; not every option appears in the sample `jkgm.json`.

| Variable | Explanation | Syntax | Default value |
|----------------|------------------------------|--------|---------------|
| **use_vanilla_renderer** | At process startup, load the DLL but do not install JkGRfxMod rendering or COG hooks. The native Direct3D renderer must work on your system. This is not a live toggle. | boolean | `false` |
| **profile_audio** | Log which A3D/DirectSound/OpenAL DLLs are already loaded during JK's menus and gameplay, including their paths, when the set changes. Does not load DLLs, change playback, or establish whether HRTF is active. Requires `log_path` to persist to disk. | boolean | `false` |
| **profile_renderer** | Log CPU frame timings and geometry counters (requires `log_path`). Disable for normal gameplay. | boolean | `false` |
| **enable_gbuffer_packing** | Consolidated compact color, emission, normal, light and pigment G-buffer layouts. Set false to compare legacy formats; restart to change. RGB11F attachments cannot represent negative colors, so verify visual parity with HDR materials. | boolean | `true` |
| **enable_saber_trails** | Draw a full-bright SideMat fan between the live and time-delayed saber tips, only during native KEY saber-damage marker windows (4=on, 5=off). Requires GPU caching and the verified native polyline hook. Swing state reads JKGM's native jkFlags directly; KEY/COG callbacks are not required. No new MAT is needed. | boolean | `true` |
| **saber_trail_delay** | Temporal delay between the live blade and its following blade edge. Their relative movement expands/collapses the fan; it does not leave a static trail. Seconds, 0.005–0.25. | float | `0.06` |
| **use_gpu_cache** | Enable unified GPU-cached 3DO meshes, ordinary sprites, and SithWorld rendering. Disable for a combined native-renderer comparison. Unsupported geometry may still require native fallback. Replaces the former `use_model_cache`, `use_sprite_cache`, and `world_cache_preview` switches. | boolean | `true` |
| **gpu_3do_vertex_format** | 3DO GPU vertex format: `0` = original 80-byte layout; `1` = consolidated packed 52-byte layout (invalid attributes fall back to 80 bytes). Historical `experimental_packed_3do_vertices` boolean remains accepted as an alias. Restart to A/B. | integer 0–1 | `1` |
| **experimental_3do_index_u16** | Use 16-bit GPU 3DO indices when the cached mesh has at most 65,536 corners and all indices fit; otherwise retain 32-bit indices. Independently switchable across main, shadow, occlusion and reflection passes. Restart to A/B. | boolean | `false` |
| **experimental_auto_3do_lod** | Generate GPU LODs for eligible 3DOs without authored LODs. Select the level using projected object radius. | boolean | `false` |
| **gpu_3do_auto_lod_lod1_radius_px** | Enter LOD1 when the whole Thing's authored model bounding-sphere **radius** projects below this many internal-screen pixels. One LOD level is shared across all hierarchy meshes, regardless of limb sizes. Higher thresholds simplify closer to the camera. Used in automatic mode with hysteresis; must exceed LOD2, maximum 4096. | float | `90.0` |
| **gpu_3do_auto_lod_lod2_radius_px** | Enter LOD2 when the whole Thing's authored model bounding-sphere **radius** projects below this many internal-screen pixels. Every hierarchy mesh shares this selection. Both thresholds require `1 <= LOD2 < LOD1 <= 4096` and have hysteresis. | float | `28.0` |
| **gpu_3do_auto_lod_debug_mode** | Debug selection for all main-camera 3DO meshes regardless of projected radius: `0` = automatic, `1` = original GeoSet 0, `2` = generated LOD1, `3` = generated LOD2. Any mesh that cannot simplify without disappearing retains its source geometry. The whole-object floor applies even in forced debug LOD modes. Effective with `experimental_auto_3do_lod=true`. | integer, 0–3 | `0` |
| **gpu_3do_auto_lod_generation_budget** | Number of new CPU LOD variant generation attempts per frame. `0` = unlimited (use to measure raw generation cost); default `1` amortizes CPU spikes across frames. Higher numbers increase throughput. Cached variants do not consume budget; this does not restrict mesh eligibility or visual quality. | integer, 0 = unlimited | `1` |
| **gpu_3do_auto_lod_min_object_triangles** | Minimum total triangles across all currently collected GPU hierarchy meshes of one owning SithThing after automatic LOD. Default `12` keeps an ordinary standalone 12-triangle box unchanged; **not a per-mesh minimum**. If the proposed result falls below the floor, the entire Thing retains its source geometry. Exact closed 12-triangle cuboids are also recognized by shape and never decimated, including when one cube is part of a larger Thing. `0` disables the whole-object floor. The count uses visible staged meshes (not unseen/culled model nodes), and the floor is an accept/reject check, not an exact triangle target. Requires restart. | integer 0–1000000 | `12` |
| **experimental_3do_occlusion_culling** | Use asynchronous main-camera occlusion queries to skip hidden cached models and ordinary sprite quads. | boolean | `false` |
| **experimental_3do_native_occlusion_culling** | Use completed GPU occlusion results to skip eligible hidden native model submissions. Requires `experimental_3do_occlusion_culling`. | boolean | `false` |
| **gpu_3do_occlusion_proxy_mode** | Occlusion proxy shape: `0` = 3D box, `1` = nearest-depth screen rectangle, `2` = center-depth screen rectangle. | integer, 0–2 | `0` |
| **gpu_3do_occlusion_query_budget** | Maximum new Thing/sprite queries per frame; `0` removes the submission budget. | integer, 0–256 | `128` |
| **gpu_3do_occlusion_debug_view** | Visualize issued proxy tests: `0` = off, `1` = wireframes, `2` = filled translucent proxies. | integer, 0–2 | `0` |
| **gpu_3do_occlusion_min_triangles** | Minimum triangle count for model occlusion queries; ordinary sprites bypass this threshold. | integer, 0–50000 | `0` |
| **experimental_planar_thing_discovery** | Log independent 3DO discovery and pose-readiness diagnostics for reflection cameras; does not draw captures. | boolean | `false` |
| **enable_reflection_capture** | Enable realtime Shared, Cube, Hemi and Planar capture/sampling. Explicit COG `CaptureEnvMap` remains independent. | boolean | `true` |
| **enable_reflection_probes** | Enable dedicated Cube/Hemi Thing probes and Planar world captures. Disabled modes use camera-centered Shared capture. | boolean | `false` |
| **reflection_capture_resolution** | Baseline `N` for Cube/Shared faces; Hemi uses `2N×2N` disks and Planar uses `N` pixels on its shorter side. | integer 32–1024 | `128` |
| **reflection_capture_update_interval** | Minimum seconds between completed realtime reflection refreshes; `0` allows refresh each frame. | float 0–60 | `0.0` |
| **envmap_capture_face_resolution** | Resolution `N×N` for each one-shot `CaptureEnvMap` view; result is `4N×2N`. | integer 32–1024 | `128` |
| **reflection_probe_max_owners** | Maximum independently retained Thing-local Hemi/Cube probes. | integer 1–16 | `4` |
| **reflection_probe_max_distance** | Thing-distance limit for dedicated Hemi/Cube captures; Shared preserves distant LEVELGEO Things. World surfaces and sky have no limit; `0` disables the Thing limit. | float: `0` or 4–4096 | `64.0` |
| **experimental_screen_space_reflections** | Trace same-frame opaque reflections in GPU-cached world and 3DO geometry. Missing rays fall back to available EnvMaps. | boolean | `false` |
| **ssr_resolution_scale** | Resolution of the SSR receiver and ray search textures relative to the internal screen. At 1920x1080, `0.125` renders approximately 240x135. This reduces the actual number of ray-traced pixels, not just the sampling resolution. | float 0.125–1.0 | `0.125` |
| **ssr_ray_steps** | Maximum coarse ray/depth checks per SSR pixel; higher is more complete but slower. | integer 4–96 | `32` |
| **ssr_ray_thickness** | World-space tolerance for deciding whether a ray intersects an opaque surface. Higher values fill small gaps but may create false hits. | float 0–8 (exclusive zero) | `0.6` |
| **ssr_max_distance** | Maximum distance (JK units) for the screen-space search; reflections beyond this fall back to the material EnvMap. | float 1–2048 | `64.0` |
| **enable_gpu_sprite_instancing** | Batch compatible GPU-cached sprites into instanced draw calls. Set `false` for A/B performance testing. | boolean | `true` |
| **enable_gpu_3do_instancing** | Instance compatible opaque 3DO groups with identical GPU mesh/material state. Translucent and incompatible groups use ordinary draws. | boolean | `false` |
| **experimental_multithreading** | Use CPU visibility workers for eligible GPU 3DO and reflection workloads. Native JK/COG logic and GL submission remain on the render thread. | boolean | `false` |
| **experimental_half_res_fluid_sprites** | Render compatible Fluid sprites at half resolution before compositing into the transparency pass. | boolean | `false` |
| **fluid_sprite_layer_budget** | Maximum surviving fluid SPR layers per half-resolution pixel using stencil. `0` means unlimited. Effective only with the half-res option; values 0–255 (e.g. 8, 16, 32). Early stencil shader rejection is driver dependent on GL 3.3. | integer | `0` |
| **enable_gpu_camera_hud** | Render Camera-type HUD 3DOs through the post-processing-exempt GPU path. Set `false` for native HUD diagnostics. | boolean | `true` |
| **enable_native_fallback** | Preserve native mixed world batches when GPU world rendering is active. Set to `false` to isolate the cached world and expose unsupported/missing faces; ordinary native fallback Things in those batches may also be hidden. | boolean | `true` |
| **gpu_pixel_lighting** | Evaluate dynamic Thing lights per pixel on GPU-cached world and 3DO geometry; disabled uses native Gouraud lighting. | boolean | `false` |
| **enable_baked_overbright** | Allow authored vertex, ambient and ExtraLight values up to 2 rather than clamping each source at 1. | boolean | `true` |
| **enable_spotlights** | Enable COG-controlled MAT-textured spotlights on eligible world and 3DO receivers (up to four active lights). | boolean | `false` |
| **spotlight_shadow_resolution** | Width and height of the spotlight shadow depth map. Higher values sharpen shadow edges and use more video memory; accepted range is `64..1024`. Takes effect when the shadow map is created. | integer | `256` |
| **enable_spotlight_shadows** | Enable spotlight shadow casting and receiving with a configurable depth map. | boolean | `false` |
| **experimental_sun_sky_caster_pruning** | Conservatively omit static world shadow casters that cannot overlap a visible sky aperture. Requires sun sky aperture sampling. | boolean | `false` |
| **experimental_sun_sky_aperture** | Gate direct sunlight through eligible sky-flagged world apertures, limiting sun contributions inside enclosed areas. | boolean | `false` |
| **enable_sun_shadows** | Enable directional sun shadow cascades for static world and eligible Things. Sunlight requires a positive COG `SetSunInfo` intensity. | boolean | `false` |
| **enable_sprite_shadows** | Enable sun-shadow casting for eligible cached sprite quads globally. Requires `enable_sun_shadows` and `use_gpu_cache`. Does not affect sprite shadow reception. | boolean | `false` |
| **sun_shadow_cull_back_faces** | Cull back-facing 3DO polygons in sunlight shadow maps. | boolean | `false` |
| **sun_shadow_cull_front_faces** | Cull front-facing 3DO triangles (keeping back faces) in sunlight shadow-map rasterization; now the default based on in-game testing. Mirrored winding is handled. May reveal artifacts on open/thin or distant sloped geometry; test with receiver/caster bias settings before altering global shadow filtering. Back-face culling remains selectable. | boolean | `true` |
| **enable_sun_dynamic_shadows** | Capture eligible moving Things into a directional sun shadow atlas in addition to world cascades. | boolean | `false` |
| **sun_dynamic_shadow_resolution** | Width and height of each of eight 4x2 atlas depth tiles, range `64..1024`. Total atlas dimensions are 4× by 2× the tile size. Defaults to `256`; `512` quadruples the dynamic atlas pixels. Independent of cascade resolution. | integer | `256` |
| **sun_dynamic_shadow_pcf_radius** | Moving-Thing sun shadow filtering: `0` = hardware bilinear comparison; positive values use tile-guarded four-tap PCF. | number | `0.0` |
| **sun_shadow_receiver_bias_scale** | Scale shadow-map receiver bias on sun cascades and moving-Thing atlas. Smaller values reduce contact gaps but may create acne. | number | `1.0` |
| **sun_shadow_caster_bias_scale** | Scale caster polygon depth bias for sun shadow maps, including moving Things. | number | `1.0` |
| **sun_shadow_near_distance** | Camera-local near sun cascade coverage in JK units, range `2..128`. Smaller extents increase nearby shadow texel density at the same map resolution but transition to the far map sooner. Test `8` versus `16`. | number | `16` |
| **sun_shadow_far_distance** | Camera-local far sun cascade coverage in JK units, range `4..512`. Must exceed `sun_shadow_near_distance`. Smaller extents improve distant cascade detail at the cost of shorter shadow reach. Test `64` versus `128`. | number | `128` |
| **sun_max_distance** | Maximum camera distance for added sunlight and its shadows in JK units; `0` means unlimited. | number | `0` |
| **sun_shadow_update_interval** | Minimum seconds between static sun cascade validation passes; `0` checks each frame. | number | `0.0` |
| **sun_shadow_resolution** | Width and height of the NEAR 24-bit sun depth map and, by default, the FAR map. Range `64..4096`. Both cascades retain separate static depth caches. Resolution never changes either cascade's world-space reach. | integer | `512` |
| **sun_shadow_far_resolution** | Far shadow-cascade depth resolution; `0` inherits the near cascade resolution. | integer | `0` |
| **sun_shadow_near_pcf_radius** | Near cascade PCF radius in texels; `0` uses one hardware-filtered shadow comparison, positive values use four taps. | number | `0.0` |
| **sun_shadow_far_pcf_radius** | Far cascade PCF radius in texels, independent of the near cascade. | number | `0.0` |
| **experimental_sun_pcf_intensity_softness** | Scale sun shadow filter softness with directional sun intensity. | number | `1.0` |
| **experimental_sun_pcf_intensity_reference** | Directional sun intensity at which adaptive shadow filters use their base radius. | number | `2.0` |
| **enable_default_specular** | Apply subtle direct-light specular to compatible GPU materials without authored specular settings. | boolean | `false` |
| **default_specular_strength** | Direct-light GGX strength for unconfigured GPU-cached MATs while `enable_default_specular=true`; zero disables the fallback. Does not change authored strengths or enable environment reflections. Range `0..1`. | number | `0.25` |
| **default_specular_roughness** | GGX roughness for unconfigured GPU-cached MATs while `enable_default_specular=true`; high values produce broad, subtle highlights. Range `0..1` (GGX clamps an effective minimum of 0.08). | number | `0.85` |
| **enable_gpu_mat_warmup** | Request deferred native MAT uploads for GPU geometry that references material cels not yet resident. | boolean | `true` |
| **enable_dynamic_light_specular** | Produce GPU-cached world/3DO specular highlights from native Thing point lights; compatible colored Thing lights affect highlight hue. | boolean | `false` |
| **enable_hdr** | Enable HDR tone mapping and optional automatic exposure. | boolean | `false` |
| **auto_exposure_sensitivity** | Controls the strength of automatic scene-adaptive exposure (0=fixed, 0..2=adaptation); bright regions, including a flashlight cone in a dark room, now influence the meter while isolated hotspots are limited. Bright adaptation is faster than dark adaptation, with at most +1.25 EV dark boost. Only active with enable_hdr=true. | number `0..2` | `0.4` |
| **hdr_exposure_compensation** | Fixed EV offset to brighten/darken the HDR image independently of adaptation; +1 doubles linear exposure. Does not change true black. | number `-3..3` | `0.0` |
| **hdr_shadow_softness** | Independent filmic shadow toe. `0` uses original crisp ACES shadows; higher values progressively soften dark regions. Applies to both color mapping styles. | number `0..1` | `0.3` |
| **hdr_color_crispness** | `0` retains hue-consistent luminance-first tone mapping; `1` uses the older punchier per-channel ACES mapping, with intermediate blends. Independent of shadow softness; strongest values may shift hues. | number `0..1` | `0.6` |
| **enable_reflections** | Master switch for replacement-renderer material reflections. Disabling also skips environment-reflection sampling where supported. | boolean | `true` |
| **retain_hom_pixels** | Preserve the previous scene color in uncovered opaque pixels, matching Jedi Knight HOM behavior. | boolean | `true` |

### Additional runtime and diagnostic settings

These additional options are also accepted by the configuration parser.

| Setting | Effect | Default |
| --- | --- | --- |
| `debug_reflection_mode` | Display the camera-centered reflection capture fullscreen: `off`, `cube`, `hemi` or `shared`. Missing capture pixels appear black. | `off` |
| `enable_post` | Master switch for renderer post-processing effects. | `true` |
| `enable_sun` | Enable additional COG-controlled directional sunlight; independent of `enable_sun_shadows`. | `true` |
| `experimental_sun_caustics` | Project procedural sun caustics through eligible Fluid world and 3DO surfaces. Requires sunlight and sun shadows. | `false` |
| `enable_blur` | Enable distance/peripheral spatial blur effects where configured by COG. | `true` |

| `enable_trails` | Enable temporal frame blending/trails where configured by COG. | `true` |
| `enable_fog` | Enable renderer fog from COG settings. | `true` |
| `experimental_radial_fog` | Measure fog using camera-to-fragment distance for world, 3DO and compatible transparent geometry. | `false` |
| `experimental_exponential_fog` | With radial fog, use smooth exponential attenuation rather than a linear fog ramp. | `false` |
| `retro_mode` | Quantize the final image to the classic CMP palette (without dithering). | `false` |
| `camera_hud_depth_sort` | Sort translucent Camera-type HUD geometry by depth; disable to preserve authored order for diagnostics. | `true` |
| `spotlight_shadow_angle_bias_scale` | Increase spotlight shadow receiver bias at grazing angles; `0` uses the base bias. Range `0..2`. | `0.0` |
| `enable_decals` | Enable geometry-projected sprite decals. | `true` |
| `enable_mesh_collision` | Enable patched JKGM type-2 3DO mesh collision handling. | `false` |
| `world_cache_fluid_debug_mode` | Fluid diagnostics for GPU-cached world materials (`0` is normal rendering). | `0` |
| `enable_normal_mapped_diffuse` | Use mapped normals for added direct lighting on GPU-cached world and 3DO materials. | `false` |
| `restore_cmp_lights` | Restore indexed fullbright MAT pixels on compatible GPU-cached rendering paths. | `false` |
| `enable_sector_crossing_lighting` | Apply local sector light, colormap and tint transitions to GPU 3DOs. | `false` |
| `experimental_sector_crossing_vertex` | Select sector lighting per vertex rather than per fragment; requires sector crossing lighting. | `false` |
| `profile_renderer_verbose` | Enable detailed renderer diagnostics in addition to the regular performance counters. | `false` |

HDR-aware bloom uses exposed linear scene luminance and composites before tone mapping.


## Sky eligibility and no-sky levels

COG sunlight and its shadow passes are suppressed in worlds with **no eligible
sky surfaces**, even when scripts set sun intensity. A surface is eligible
when it has sky flags 0x200/0x400 **and no adjoin pointer**. GeoMode does not
affect eligibility: invisible (GeoMode 0) non-adjoin sky faces remain valid
openings for custom 3DO skyboxes, while sky-flagged adjoins do not. The same
rule controls the sky aperture and sky-first coverage measurements. Sun COG
values remain stored for later levels. Native surface-table read failures
conservatively leave sunlight enabled rather than disabling it by accident.

## Sprite sunlight and shadowing

Cached world sprites receive direct sunlight and sample existing sun shadow maps,
including alpha-blended smoke (normal or Fluid) and alpha-cutout vegetation,
without adding shadow casters. Fluid smoke receives sunlight on its diffuse RGB
before the existing depth-based absorption, with its alpha equation unchanged. Camera-type sprites and sprites with ThingFlag `0x20` remain exempt.

**Sprite casting is off by default.** Set `"enable_sprite_shadows": true`
in `jkgm.json` to enable it globally. Casting requires `use_gpu_cache`,
`enable_sun_shadows`, a resident MAT, and an eligible opaque/cutout ordinary
sprite (types 0, 1 or 2). Projected decals, flares, Camera HUD Things,
ThingFlag `0x20`, and partially transparent sprite instances do not cast.
The switch controls only casting; normal sprite shading and shadow reception
are unaffected. No sprite-specific COG verbs or per-Thing shadow state are
required.


GPU mesh mutation tracking and adaptive HUD dirty-rectangle updates operate automatically. GPU caching, Camera HUD, sprite instancing, MAT warmup and native fallback remain individually configurable.


### Standard sunlight shadow optimizations

When `enable_sun_shadows` is enabled, the following optimizations are always
active without configuration switches: stable cascade projection reuse (only
while the entire camera frustum remains inside the cached projection), cached
static world/stationary-3DO depth layers, compatible repeated-mesh shadow
instancing, and adaptive per-Thing dynamic atlas tiles when
`enable_sun_dynamic_shadows` is also enabled. The static layers automatically
fall back to full-cascade redraws when the extra framebuffers cannot be used.
Animated world geometry must invalidate the static shadow cache when its geometry changes.

Keep `sun_shadow_resolution`, `sun_dynamic_shadow_resolution`, near/far
cascade distances, shadow depth biases, and the shadow enable switches for
quality/performance tuning.
