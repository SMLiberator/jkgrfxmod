# Installing JkGRfxMod (standalone development build)

JkGRfxMod now starts from `jkgm.exe` **directly**. The old injection launcher
is not built or packaged. The renderer still lives in `renderer.dll`; the
executable loads that DLL on startup. "Standalone" means no separate launcher
or injection executable, **not** a single file with the DLL embedded.

## Requirements

- Your own copy of the **specific patched** 32-bit `jk23.exe` validated by
  `tools/patch_jkgm_bootstrap.py` (SHA-256
  `1610d070112b7f3a78668ede5deab9aeebc758e83ec4c69aa705c2e91f64a0da`).
  Other JK/MotS/Steam/GOG builds are **not** supported by this particular patcher.
- Python 3 (only needed to create `jkgm.exe`).
- The matching x86 `renderer.dll`, `jkgm.json`, and `jkgm/` assets
  from the same JkGRfxMod build.

## Setup

1. Back up your game EXE. Put `jk23.exe` in your Jedi Knight installation
   directory alongside the game data.
2. Copy `renderer.dll`, `jkgm.json` and the `jkgm/` assets to that
   directory. Use the current shaders from the same build as the DLL.
3. In a terminal opened in the **game directory**, run:

   ```sh
   python3 patch_jkgm_bootstrap.py jk23.exe jkgm.exe
   ```

   On Windows, use `py -3` or `python` if `python3` is unavailable.
   The script is included in development/release artifacts and lives at
   `tools/patch_jkgm_bootstrap.py` in the repository.
4. Launch `jkgm.exe` directly, passing your usual Jedi Knight command-line
   switches (such as `-path gunrunner` if your mod requires it).
   Do **not** use `inject.exe` or patch `jkgm.exe` a second time.

   ```sh
   wine jkgm.exe -path gunrunner
   ```

   The example uses Wine; on Windows you can run `jkgm.exe` directly.

## Renderer selection

In `jkgm.json`, use `"use_vanilla_renderer": false` for JkGRfxMod (default).
Set it to `true` **before launching** to skip the renderer/COG hooks and use
JK's original rendering path. The game must be restarted for this setting to
take effect. If the DLL cannot be loaded, the bootstrap falls back to the
original startup path; it does not show an error dialog.

The JSON `command` field belonged to the old launcher and is no longer used.
See [config.md](config.md) for all current settings.

## Distribution and limitations

- GitHub builds intentionally **do not include** a patched Jedi Knight game EXE.
  Users generate their own `jkgm.exe` from the validated game binary.
- The bootstrap patcher refuses unknown input hashes and will not patch an
  already-patched EXE. Keep `jk23.exe` for regeneration and rollback.
- The current standalone bootstrap has been tested with the specified user's
  executable, not generalized to all games. Native Direct3D rendering may
  independently fail on particular Wine/driver configurations.


## Experimental per-mesh collision (JKGM only)

The current `development` build includes a first **opt-in**, untested-in-game
`collide=2` sphere-tree hook. To try it only in a disposable test level,
change `jkgm.json` and restart:

```json
"enable_mesh_collision": true,
"profile_renderer": true,
"log_path": "jkgm-collision.log"
```

With the default `"enable_mesh_collision": false`, no native Thing
collision detour is installed. The option is independent of
`"use_model_cache"`: GPU model caching is not required.

The hook is signature-locked to this patched executable, and handles
collision flags except **0x80** (force-sphere) and **0x400** (special
hit-distance convention). It uses current animated node matrices,
a fitted sphere around each mesh's local vertex geometry, and the
ordinary native Thing hit bit, hit distance and normal. As in MotS,
mesh/face output pointers are not written for tree-sphere collisions.
For unsupported flags, models, or failed geometry/pose reads, the
original JK collision function is called.
Modes 1 and 3 are not redirected.

A bounded diagnostic sample appears as `JKGM collide=2 stage=`:
`hit`, `miss`, `invalid_3do_snapshot`,
`invalid_pose_or_query`, or `invalid_hit_pointer`. A missing hook
signature also produces a warning when this option is on.

The initial version rebuilt every mesh on every query and caused severe
slowdowns around animated actors. The optimized implementation now
**fits each mesh's local center and radius once per model/geoset** and
shares that geometry between Things using the same 3DO. It discards
its copies of source vertices after fitting, retaining only the
collision-only offsets, radii and hierarchy relationships. It does
**not** rewrite any actual 3DO pivots or mesh radii (which would
change animation/rendering). Live animation matrix transforms and
amputation flags are still evaluated per query.

The bounded model cache holds up to 24 selected model/geoset instances,
and invalidates when the loaded model identity changes. It does not
automatically pick up in-place COG modifications to a mesh's raw
vertices until the model/geoset is replaced or the renderer restarts;
there is no recurring full-vertex refit. The
`JKGM collide=2 cache` log shows reuse/rebuild/failure/eviction counts
when profiling is enabled. Models over 262144 total vertices fall back
to native collision handling. In-game performance remains experimental. There is no new `treeSize` field. Upstream
sector candidate filtering and unusual collision flags still need
additional live testing. Back up files and disable the switch to
restore original collision behavior.


## GetThingAttachNormal COG verb (all collision modes)

`GetThingAttachNormal(thing)` returns the **world-space unit normal
of the face the Thing is currently attached to**. World-surface
attachments return the level surface's actual `rdFace.normal`;
Thing-face attachments transform the attached object's local face
normal through its current orientation. A zero vector means the Thing
has no face attachment or the attachment data could not be read.
Ordinary Thing-to-Thing attachments (without a face) return zero.

```c
vector normal = GetThingAttachNormal(player);
if (VectorLen(normal) > 0.001)
    PrintVector(normal);
```

This getter works with `"enable_mesh_collision": false`, because it
reads JK's existing attachment state. It is **not** the last raycast
normal and does not pretend that a `collide=2` *sphere* hit has a
polygon normal. With `collide=3`, or when a Thing is attached to a
real world surface, it provides the geometric normal needed for
alignment.


## Icy surfaces on 3DO Things (experimental)

For a standable 3DO, use **ThingFlag `0x80000000`** to make its faces
slippery when another physics Thing is attached to one of its polygon
faces. Include `0x40` (STANDABLE) as appropriate:

```
thingflags=0x80000040
collide=3
```

This is a custom JKGM extension, not a newly discovered vanilla
ThingFlag meaning. We verified that the tested JKGM rendering loop
does **not** hide objects with bit `0x80000000` (although the
OpenJKDF2 renderer does). That bit still has not been proven unused
throughout every subsystem or third-party patch.

The signature-guarded detour at `0x004F7833` modifies the native
friction multiplier **only for Thing-face attachments**. Ordinary
Icy world surfaces (`surfaceflags=0x1000`) already select
`friction=0.3`; Very Icy world surfaces (`0x2000`) select
`friction=0.1`. No new Very Icy **ThingFlag** is defined.

Native attached-physics drag behavior:

- `effectiveSurfDrag = surfDrag * friction`.
- Under ordinary no-acceleration/no-sector-thrust conditions,
  `StaticDrag` supplies a low-speed stopping threshold on normal
  surfaces, but the native physics branch disables that threshold
  when `friction <= 0.8`. Consequently, both normal native Icy and
  Very Icy surfaces **ignore StaticDrag** while maintaining an
  attachment.
- The stored `surfDrag` and `staticDrag` parameters are
  **not modified**; the coefficient is only applied while attached.

The new custom ThingFlag uses the same `0.3` multiplier as native
Icy, so it also causes `StaticDrag` to be ignored in that branch.
World-face attachments and ordinary Thing-to-Thing attachments
are unchanged. `collide=2` sphere-tree contacts are not face
attachments and do not receive this feature by themselves.

For diagnosis, a successful hook installation logs
`JKGM Thing-face icy flag enabled (0x80000000)`. This behavior
has regression tests but still needs a controlled gameplay test.

## Native `hitearth` sound routing (JKGM)

The original surface-impact handler distinguishes `hitmetal` (SoundClass
47) from `hithard` (46), but it never selects `hitearth` (48)
even on Earth surfaces (`surfaceflags & 0x80000`). The signature-guarded
DLL detour at `0x004E8EBA` preserves native damage checks, sound volume,
metal priority and the `touched` callback, changing only the nonmetal
sound mode when the hit surface is Earth. It is enabled on the supported
JKGM executable automatically (no separate EXE patch).

The `scrapehard`, `scrapemetal`, and `scrapeearth` soundclass modes
(50/51/52) are present in the enumerations but have no observed
automatic call sites in the reference engine. We have **not** added
scraping sounds; those need sustained sliding contact and loop control.

## Experimental projected SPR decals (world architecture only)

To opt in to the initial GPU-submitted, geometry-clipped type-3 SPR
projector, add:

```json
"enable_sprite_decals": true
```

This option is independent of `"enable_mesh_collision"` and
`"use_model_cache"`. Give an SPR authored `type 3` a nonzero
projection width/height, place it facing a **real JK world polygon**
(not a flat 3DO), and ensure the original SPR MAT is resident.
The first valid projector scans world faces, clips a polygon mesh to
the 0.25-unit depth window, reuses the SPR's MAT/UVs, and sends it
to the GPU alpha pass. Sprite opacity/roll/scale affect the result.
The clip mesh is cached by world/Thing/pose. If projection is invalid
or no world polygon qualifies, the ordinary native sprite is drawn.

Set `"profile_renderer": true` and look for
`SPR decal GPU stage=geometry` with positive receiver and triangle
counts, followed by `SPR decal GPU stage=queued` when the native MAT
has become resident and GPU submission actually occurs. The new GPU rendering path is **not yet validated in-game**;
3DO receivers, animated receivers, and interactions with transparent
surfaces need separate implementation.
