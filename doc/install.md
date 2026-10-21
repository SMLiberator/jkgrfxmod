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

This initial implementation **rebuilds its mesh snapshot per relevant
collision query**. Avoid large, busy scenes until geometry caching and
invalidation are implemented; performance and actual JKGM behavior
are not yet verified. There is no new `treeSize` field. Upstream
sector candidate filtering and unusual collision flags still need
additional live testing. Back up files and disable the switch to
restore original collision behavior.
