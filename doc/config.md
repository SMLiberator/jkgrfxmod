# Editing configuration file (`jkgm.json`)

| Variable | Explanation | Syntax | Default value |
|----------------|------------------------------|--------|---------------|
| **resolution** | Output resolution. The bundled sample `jkgm.json` specifies 1024×768; if the key is absent, the built-in fallback is 640×480. | ordered pair (`[ x, y ]`) | Built-in: `[ 640, 480 ]`; sample: `[ 1024, 768 ]` |
| **fullscreen** | Select whether to use fullscreen or windowed mode. | boolean (`true` for fullscreen, `false` for windowed) | `false` |
| **correct_game_aspect_ratio** | Select whether the game should use a 4:3 aspect ratio. | boolean<br/>(`true` for a 4:3 aspect ratio, or `false` to fill the screen) | `false` |
| **correct_menu_aspect_ratio** | Select whether menus and cutscenes should be stretched to fill the screen. | boolean<br/>(`true` for the original aspect ratio, or `false` for stretched) | `true` |
| **hud_scale** | Upscale the HUD to avoid tiny HUD at high resolutions. Use whole integers for best results. | decimal<br/>(ex: 2.0 = 200% scaling) | Default: `1.0` |
| **max_anisotropy** | Level of [anisotropic filtering](https://en.wikipedia.org/wiki/Anisotropic_filtering) to use. Can be used even when `enable_texture_filtering` is set to false. | decimal (values taken from [ARB_texture_filter_anisotropic](https://www.khronos.org/registry/OpenGL/extensions/ARB/ARB_texture_filter_anisotropic.txt)): `1.0` to disable,<br/> `2.0` for 2x,<br/> `4.0` for 4x,<br/> `8.0` for 8x,<br/> `16.0` for 16x | Default: `2.0` |
| **antialiasing** | Type and degree of [antialiasing](https://en.wikipedia.org/wiki/Spatial_anti-aliasing) to use. | `null` or object (`{ "type": <type>, "samples": <integer> }`)<br/><br/>**Supported types:**<br/>`MSAA`: Multisample antialiasing smooths the edges of polygons only.<br/>`SSAA`: Supersample antialiasing smooths polygon edges and interiors, but is much more expensive than MSAA.<br/><br/>**Other fields:**<br/>`samples`: The number of antialiasing samples per pixel. Higher values may look better, but are more expensive.<br/>`min_sample_factor`: When used with MSAA, applies some amount of supersampling to polygon interiors. For example, 8xMSAA with a minimum sample factor of 0.25 will apply 2xSSAA to polygon interiors. This can be used to gain some antialiasing for polygon interiors without the full cost of SSAA.| Default: `null` |
| **enable_vsync** | Enable vsync. Due to *Jedi Knight*'s low internal framerate, enabling this option may cause stuttering and dropped frames. | boolean | `false` |
| **use_vanilla_renderer** | At process startup, load the DLL but do not install JkGRfxMod rendering or COG hooks. The native Direct3D renderer must work on your system. This is not a live toggle. | boolean | `false` |
| **profile_audio** | Log which A3D/DirectSound/OpenAL DLLs are already loaded during JK's menus and gameplay, including their paths, when the set changes. Does not load DLLs, change playback, or establish whether HRTF is active. Requires `log_path` to persist to disk. | boolean | `false` |
| **profile_renderer** | Log CPU frame timings and geometry counters (requires `log_path`). Disable for normal gameplay. | boolean | `false` |
| **enable_saber_trails** | Draw a full-bright SideMat fan between the live and time-delayed saber tips, only during native KEY saber-damage marker windows (4=on, 5=off). Requires GPU caching and the verified native polyline hook. Swing state reads JKGM's native jkFlags directly; KEY/COG callbacks are not required. No new MAT is needed. | boolean | `true` |
| **saber_trail_delay** | Temporal delay between the live blade and its following blade edge. Their relative movement expands/collapses the fan; it does not leave a static trail. Seconds, 0.005–0.25. | float | `0.06` |
| **use_gpu_cache** | Enable unified GPU-cached 3DO meshes, ordinary sprites, and SithWorld rendering. Disable for a combined native-renderer comparison. Unsupported geometry may still require native fallback. Replaces the former `use_model_cache`, `use_sprite_cache`, and `world_cache_preview` switches. | boolean | `true` |
| **enable_gpu_sprite_instancing** | Batch compatible GPU-cached sprites into instanced draw calls. Set `false` for A/B performance testing. | boolean | `true` |
| **enable_gpu_camera_hud** | Render Camera-type HUD 3DOs through the post-processing-exempt GPU path. Set `false` for native HUD diagnostics. | boolean | `true` |
| **enable_native_fallback** | Preserve native mixed world batches when GPU world rendering is active. Set to `false` to isolate the cached world and expose unsupported/missing faces; ordinary native fallback Things in those batches may also be hidden. | boolean | `true` |
| **gpu_pixel_lighting** | When enabled, use shared per-pixel dynamic point-light GLSL for supported GPU-cached 3DOs **and** cached world surfaces. When disabled, preserve JK-style vertex Gouraud dynamic lighting. Does not change native-rendered geometry. | boolean | `false` |
| **enable_spotlights** | EXPERIMENTAL: enable COG MAT-textured perspective spotlights attached to individual Things (one per Thing); Things can independently follow animated model nodes via AttachThingToThingNode. Up to four active lights per level; opaque world/3DO receivers through the shared depth/normal buffer. A verified native JKGM cache-loader attempts MAT residency when SetSpotlight is called and retries during rendering if the device is not yet initialized; native cache entries are refreshed while the light is active. The spotlight blends against stored unlit material pigment plus a small additive glow, instead of merely adding a flat screen overlay. The loader is gated on tested executable signatures (the previous unverified 0x0044AA70 call remains removed). Runtime behavior is experimental. Translucent blended receivers are supported without a new pass; optional spotlight shadows use `enable_spotlight_shadows`, and ordinary translucent faces/sprites now receive diffuse sunlight and existing sun-map occlusion when sunlight is enabled on compatible hardware. | boolean | `false` |
| **spotlight_shadow_resolution** | Width and height of the spotlight shadow depth map. Higher values sharpen shadow edges and use more video memory; accepted range is `64..1024`. Takes effect when the shadow map is created. | integer | `256` |
| **enable_spotlight_shadows** | EXPERIMENTAL: when spotlights are enabled, use a single 256px (configurable to 1024px) 24-bit hardware-filtered depth map for opaque receivers, casting from level architecture and captured 3DO meshes. Projector/caster state is cached; static or parked casters do not force a redraw. Masked world adjoins and 3DOs use MAT alpha-tested depth silhouettes; blended receivers sample the same four-tap filtered shadow map. Runs with native or GPU-cached 3DOs; native mode uses a shadow-only mesh snapshot without changing the visible renderer. Disabled by default. | boolean | `false` |
| **enable_sun_shadows** | EXPERIMENTAL: add direct diffuse sunlight independently of shadow-map readiness, with two-sided world geometry casting and optional two-cascade shadows: a sharper near 16-unit and broader far 128-unit orthographic map, each at sun_shadow_resolution, sharing static caster geometry. World-only cascade depth is now cached separately from moving 3DO casters and GPU-blitted into each working depth map, with a full-redraw fallback if the extra FBOs are unsupported. Both use 24-bit depth and hardware-filtered PCF, using a single bilinear depth comparison for near character silhouettes and softer multi-tap filtering for distant geometry. Model-only levels with no native surfaces are supported. COG Thing flag `0x20` marks skybox/UI Things, which remain visible but do not cast directional sunlight or spotlight shadows. On GPU-cached 3DOs it also suppresses added sunlight/specular and projected spotlights and prevents SSAO reception by writing a zero G-buffer normal; native-rendered 3DOs still require per-Thing G-buffer tagging for the same receiver exclusions. It does not change JK's authored vertex lighting or other visual material properties. The existing SSAO distance fade remains enabled. The map still covers the level's light-axis depth so nearby offscreen geometry can cast into the view. Surfaces beyond that range retain diffuse sunlight without projected shadows. Ordinary alpha-blended world faces/sprites and GPU-cached 3DOs receive forward diffuse sunlight with near/far cascade and dynamic Thing atlas shadows where supported, excluding flares, decals, additive/multiply and post-exempt draws; Fluid receives the same diffuse sunlight and shadows in the native and GPU-cached 3DO/sprite forward paths (GPU-cached world Fluid remains unchanged); fully transparent casters remain excluded. The forward sun receiver uses texture units 16..18 for shadow maps, requiring at least 19 fragment sampler units (otherwise it still adds diffuse sunlight without shadow sampling). Occluded surfaces retain part of JK's baked vertex lighting as ambient while a bounded portion is darkened. Requires a positive `SetSunInfo` intensity; the sun defaults to zero. Disabled by default. | boolean | `false` |
| **enable_sprite_shadows** | Enable sun-shadow casting for eligible cached sprite quads globally. Requires `enable_sun_shadows` and `use_gpu_cache`. Does not affect sprite shadow reception. | boolean | `false` |
| **sun_shadow_cull_back_faces** | Cull back-facing 3DO triangles when generating sunlight shadow maps, including the dynamic Thing atlas. Disabled by default because front-face culling generally produces better closed-model results. Set back=true, front=false to recover the previous behavior. Both false renders both face orientations for eligible 3DO groups; both true is invalid. Native world shadow faces stay two-sided. | boolean | `false` |
| **sun_shadow_cull_front_faces** | Cull front-facing 3DO triangles (keeping back faces) in sunlight shadow-map rasterization; now the default based on in-game testing. Mirrored winding is handled. May reveal artifacts on open/thin or distant sloped geometry; test with receiver/caster bias settings before altering global shadow filtering. Back-face culling remains selectable. | boolean | `true` |
| **enable_sun_dynamic_shadows** | EXPERIMENTAL: add an eight-Thing directional shadow atlas (configurable resolution per Thing) over existing architectural cascades. Eligible movers have SithThing.moveType other than 0; all meshes of one Thing share a tile. 24-bit bilinear depth comparison; static cascades remain as fallback. Atlas-selected Things no longer cast duplicate blurry silhouettes into the broad cascades, while overflow retains the old shadow fallback. Slope-aware depth offset helps grazing-angle acne. Only Things submitted by JK's model traversal are captured (offscreen models can be missed). Disable for A/B performance checks. | boolean | `false` |
| **sun_dynamic_shadow_resolution** | Width and height of each of eight 4x2 atlas depth tiles, range `64..1024`. Total atlas dimensions are 4× by 2× the tile size. Defaults to `256`; `512` quadruples the dynamic atlas pixels. Independent of cascade resolution. | integer | `256` |
| **sun_shadow_receiver_bias_scale** | Experimental multiplier for receiver-side depth bias on both cascades and the dynamic sunlight atlas. Lower values can reduce bright contact gaps (peter-panning) but risk surface acne. Range `0..2`, default `1`. Try `0.5` and `0.25` for comparisons. Zero retains only the minimum depth precision allowance. | number | `1.0` |
| **sun_shadow_caster_bias_scale** | Experimental multiplier for the rasterizer's slope-scale and constant polygon offset when generating sunlight depth maps, including the dynamic atlas. Lower values move occluder depth closer to actual surfaces and can reduce detached shadows; may increase shadow acne. Range `0..2`, default `1`. | number | `1.0` |
| **sun_shadow_near_distance** | Camera-local near sun cascade coverage in JK units, range `2..128`. Smaller extents increase nearby shadow texel density at the same map resolution but transition to the far map sooner. Test `8` versus `16`. | number | `16` |
| **sun_shadow_far_distance** | Camera-local far sun cascade coverage in JK units, range `4..512`. Must exceed `sun_shadow_near_distance`. Smaller extents improve distant cascade detail at the cost of shorter shadow reach. Test `64` versus `128`. | number | `128` |
| **sun_max_distance** | Optional hard range for added sunlight and sun shadows, in JK units of camera forward distance. `0` (default) keeps the previous unlimited behavior. When positive, added diffuse sun and its shadow darkening are full strength to 85% of this distance, fade smoothly to zero at it, and are absent beyond it, so the level's authored vertex lighting is left untouched there. Pixels beyond the range skip the sun shadow lookups entirely, and the far cascade is clamped to `min(sun_shadow_far_distance, sun_max_distance)` so its texels cover a smaller area (sharper shadows). Applies to opaque receivers and to translucent receivers. Must be `0` or between 4 and 512 and exceed `sun_shadow_near_distance`; values at or below `sun_shadow_far_distance` give the most consistent result. | number | `0` |
| **sun_shadow_update_interval** | EXPERIMENTAL: minimum real-time seconds between full sun-shadow cascade validation passes, range `0..2`. `0` preserves every-frame checking; try `0.1`. Skipped frames reuse the cached near/far depth maps and their exact projections, but the dynamic Thing shadow atlas is still rebuilt every frame. World changes, sun-direction changes, cache invalidity and camera leaving cached cascade coverage force immediate validation. This can trade smoother average CPU time for occasional expensive validation frames and up to the configured delay for changes in stationary caster geometry/materials. | number | `0.0` |
| **sun_shadow_resolution** | Width and height of EACH camera-local sun shadow cascade depth map (two 24-bit maps). Higher values sharpen shadow edges and use more video memory; accepted range is `64..4096`. Large values use more GPU memory, especially with static layer caching (four depth textures). | integer | `512` |
| **enable_default_specular** | EXPERIMENTAL: add broad, weak GGX highlights on previously unconfigured GPU-cached 3DO and world MATs, with no EnvMap requirement. Preserves explicit reflection settings (including authored zero), surface maps and metalness. Excludes sky, HUD, SPR effects, decals, additive/multiply, fully bright and exempt Things. Requires `enable_reflections`, direct sun or optional Thing-light specular. | boolean | `false` |
| **default_specular_strength** | Direct-light GGX strength for unconfigured GPU-cached MATs while `enable_default_specular=true`; zero disables the fallback. Does not change authored strengths or enable environment reflections. Range `0..1`. | number | `0.25` |
| **default_specular_roughness** | GGX roughness for unconfigured GPU-cached MATs while `enable_default_specular=true`; high values produce broad, subtle highlights. Range `0..1` (GGX clamps an effective minimum of 0.08). | number | `0.85` |
| **enable_gpu_mat_warmup** | Warm native MAT/cel textures required by GPU-cached geometry using the guarded post-native-Thing-tick uploader (enabled by default). All missing cels in a visible 3DO mesh are queued together, with up to 12 uploads per tick and a ~2ms soft time budget. Failed uploads retain the 30-tick cooldown; skipped requests retry next tick. With native fallback off, missing meshes may still briefly be invisible. | boolean | `true` |
| **enable_dynamic_light_specular** | EXPERIMENTAL: allow nearby dynamic Thing lights to produce GGX specular highlights on GPU-cached 3DO materials (requires `enable_reflections` and `use_gpu_cache`). Lights remain neutral/white because JK exposes only scalar intensity. GPU-cached SithWorld materials now also support these GGX highlights without an EnvMap. Native 3DOs remain unchanged. | boolean | `false` |
| **enable_hdr** | Enable the filmic HDR display transform with fixed exposure even if automatic adaptation is disabled. All other HDR settings are ignored unless this is true; also ignored if post-processing is off or Retro CMP active. | boolean | `false` |
| **auto_exposure_sensitivity** | Controls the strength of automatic scene-adaptive exposure (0=fixed, 0..2=adaptation); bright regions, including a flashlight cone in a dark room, now influence the meter while isolated hotspots are limited. Bright adaptation is faster than dark adaptation, with at most +1.25 EV dark boost. Only active with enable_hdr=true. | number `0..2` | `0.4` |
| **hdr_exposure_compensation** | Fixed EV offset to brighten/darken the HDR image independently of adaptation; +1 doubles linear exposure. Does not change true black. | number `-3..3` | `0.0` |
| **hdr_shadow_softness** | Independent filmic shadow toe. `0` uses original crisp ACES shadows; higher values progressively soften dark regions. Applies to both color mapping styles. | number `0..1` | `0.3` |
| **hdr_color_crispness** | `0` retains hue-consistent luminance-first tone mapping; `1` uses the older punchier per-channel ACES mapping, with intermediate blends. Independent of shadow softness; strongest values may shift hues. | number `0..1` | `0.6` |
| **enable_bloom** | Use [bloom](https://en.wikipedia.org/wiki/Bloom_(shader_effect)) effect for brighter lights. Requires compatible texture pack. | boolean | Default: `true` |
| **experimental_fused_bloom_downsample** | EXPERIMENTAL: merge full-resolution bright-pixel extraction with the first 2x bloom downsample, avoiding one fullscreen pass and full-resolution intermediate write. Thresholds each input pixel before averaging to closely match baseline bloom, including HDR exposure. Requires bloom and post-processing; uses the legacy sequence if GL scissor testing is enabled. | boolean | `false` |
| **experimental_skip_bloom_clears** | EXPERIMENTAL: skip clears of temporary bloom buffers completely overwritten by fullscreen draws, retaining clears when scissor testing is active. Final output clears are unchanged. Requires bloom and post-processing. | boolean | `false` |
| **experimental_native_triangle_uploads** | EXPERIMENTAL: do not map/upload empty legacy triangle batches; size populated GPU buffers using the current used triangle count rather than spare CPU capacity. GPU 3DO cached rendering and the CPU triangle layout are unchanged. | boolean | `false` |
| **enable_reflections** | Master switch for replacement-renderer material reflections. Disabling also skips environment-reflection sampling where supported. | boolean | `true` |
| **retain_hom_pixels** | Vanilla-like HOM (the default): preserve the existing linear scene color buffer from the previous frame in pixels where opaque geometry did not draw. No new texture, no copy, no extra pass; two scene-color clears are skipped. Depth and G-buffer are still reset, and menu/game transitions invalidate retained scene colors. Actual missing geometry can leave moving-camera trails and streaks. Set `false` to reproduce vanilla JK's advanced **Clear Display** option: clear empty regions to black instead of retaining HOM. For best thin-crack appearance try together with `fill_tjunction_gaps`. | boolean | `true` |
| **debug_void_pixels** | Neon magenta/lime checkerboard diagnostic on uncovered opaque G-buffer pixels after opaque composition, overriding black clear/HOM retention. Helps locate missing geometry and distinguish uncovered cracks from painted surfaces. Alpha-blended-only pixels have no opaque depth and can also show this diagnostic underneath them. Disable for normal gameplay. | boolean | `false` |
| **enable_ssao** | Use [screen-space ambient occlusion](https://en.wikipedia.org/wiki/Screen_space_ambient_occlusion) to subtly increase perceived depth in the image. | boolean | `true` |
| **enable_parallax** | Use [parallax occlusion mapping](https://en.wikipedia.org/wiki/Parallax_occlusion_mapping) to add depth to individual textures. Requires compatible texture pack. | boolean | `true` |
| **enable_texture_filtering** | Use [bilinear filtering](https://en.wikipedia.org/wiki/Bilinear_filtering) on textures, reducing texture aliasing whilst also making closeup textures blurry.  Disable to use nearest-neighbor filtering instead.<br/>Note that it does not affect [trilinear filtering](https://en.wikipedia.org/wiki/Trilinear_filtering), which is always used, or anisotropic filtering, which is controlled with `max_anisotropy`. | boolean | `true` |
| **enable_posterized_lighting** | Posterize the lighting, introducing noticeable [color banding](https://en.wikipedia.org/wiki/Colour_banding) that simulates the 8-bit lighting of the RenderDroid software renderer. | boolean | `false` |
| **vram_texture_preload_size** | Preloads materials up to a specified amount of video memory. This may reduce stuttering at run-time if material packs are installed on a slow hard drive, but will increase game startup time. | number (megabytes) | `0` |

| **data_path** | Path to material and shader patches.<br/>Accepts both absolute and relative [paths](https://en.wikipedia.org/wiki/Path_(computing)#MS-DOS/Microsoft_Windows_style). | `"<folderpath>"` | `"jkgm"` |
| **log_path** | Path to log file.<br/>Accepts both absolute and relative [paths](https://en.wikipedia.org/wiki/Path_(computing)#MS-DOS/Microsoft_Windows_style). | `"<folderpath>"`, or `null` to not output to a log | `null` |


### Additional runtime and diagnostic settings

The following options are accepted by the configuration parser but were
missing from the table above. Defaults are taken from `common/config.hpp`;
not all options appear in the minimal sample `assets/jkgm.json`.

| Setting | Effect | Default |
| --- | --- | --- |
| `enable_post` | Master switch for renderer post-processing effects. | `true` |
| `enable_sun` | Enable additional COG-controlled directional sunlight; independent of `enable_sun_shadows`. | `true` |
| `enable_blur` | Enable distance/peripheral spatial blur effects where configured by COG. | `true` |
| `experimental_spatial_blur_half_res` | EXPERIMENTAL: run the horizontal depth-aware Gaussian blur at half width/height, then perform the vertical pass at full resolution. Unblurred pixels are copied from the original full-res scene, so areas with no blur stay sharp; active blurred regions may look softer or have changed edge transitions. Uses separate shader and intermediate target, allocated only when enabled; disabled automatically if GL scissor testing is active. Does **not** change HDR exposure, tone mapping, bloom, color grading, or HUD processing. A/B-test screenshots and the `present_gpu_us blur` profiler counter. | `false` |
| `enable_trails` | Enable temporal frame blending/trails where configured by COG. | `true` |
| `enable_fog` | Enable renderer fog from COG settings. | `true` |
| `retro_mode` | Quantize the final image to the classic CMP palette (without dithering). | `false` |
| `camera_hud_depth_sort` | Sort translucent Camera-type HUD geometry by depth; disable to preserve authored order for diagnostics. | `true` |
| `spotlight_shadow_angle_bias_scale` | Experimental receiver-side grazing-angle spotlight shadow bias; `0` preserves previous behavior. Range `0..2`. | `0.0` |
| `enable_decals` | Enable type-3 projected sprite decals; legacy `enable_sprite_decals` is accepted if the new key is absent. | `true` |
| `enable_mesh_collision` | Enable experimental patched JKGM type-2 mesh collision handling. | `false` |
| `world_cache_fluid_debug_mode` | Fluid diagnostics for GPU-cached world materials (`0` is normal rendering). | `0` |
| `enable_normal_mapped_diffuse` | Experimental mapped normals for added direct lighting on cached world and 3DO materials. | `false` |
| `restore_cmp_lights` | Restore indexed fullbright MAT pixels on compatible GPU-cached rendering paths. | `false` |
| `enable_sector_crossing_lighting` | Experimental per-fragment 3DO sector light/CMP/tint transitions. | `false` |
| `profile_renderer_verbose` | Enable detailed renderer diagnostics in addition to the regular performance counters. | `false` |

<!-- No need to really have this, but I didn't want to remove it yet, in
## HUD Scaling
Recommend values for HUD scaling (based on 360p HUD size and Jedi Outcast/Academy HUD sizes): `2.0` for 720p, `3.0` for 1080p, `4.0` for 1440p, `6.0` for 2160p.
-->

Bloom brightness extraction now measures HDR-exposed linear scene luminance using the same fixed EV and adapted 1x1 EV as the final display pass. It still adds bloom before tone mapping and never double-exposes the bloom contribution. Bloom threshold and existing weights are unchanged when HDR is off.


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


The previous `mesh_revision_tracking` and `optimize_hud_uploads` switches are
no longer configurable: JK.dll mesh revision tracking (with audited fallback)
and HUD slab-delta uploads are standard. Legacy configuration keys are ignored
with a warning. `use_gpu_cache`, `enable_gpu_camera_hud`,
`enable_gpu_sprite_instancing`, `enable_gpu_mat_warmup`, and
`enable_native_fallback` are enabled by default and individually configurable.


### Standard sunlight shadow optimizations

When `enable_sun_shadows` is enabled, the following optimizations are always
active without configuration switches: stable cascade projection reuse (only
while the entire camera frustum remains inside the cached projection), cached
static world/stationary-3DO depth layers, compatible repeated-mesh shadow
instancing, and adaptive per-Thing dynamic atlas tiles when
`enable_sun_dynamic_shadows` is also enabled. The static layers automatically
fall back to full-cascade redraws when the extra framebuffers cannot be used.
A slightly less optimal shadow texel density is possible while a projection is
reused. In-place animated SithWorld vertex geometry must still invalidate or
rebuild the static shadow cache to avoid stale shadows. The former four
optimization switches are accepted only as deprecated, ignored keys.

Keep `sun_shadow_resolution`, `sun_dynamic_shadow_resolution`, near/far
cascade distances, shadow depth biases, and the shadow enable switches for
quality/performance tuning.
