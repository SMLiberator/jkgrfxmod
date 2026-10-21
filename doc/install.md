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
