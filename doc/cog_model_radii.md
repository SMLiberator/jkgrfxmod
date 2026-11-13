# COG model and mesh radius setters (JKGM)

Two new verbs are registered by JkGRfxMod's existing JK.dll `InitNewCogVerbs` wrapper:

```c
SetModelRadius(model, radius);
SetModelMeshRadius(model, mesh_index, radius);
```

Both setters return no value. They modify **shared model attributes**, not
`Thing size` or `movesize`. Model instances referencing the same model
entry see the same change. Call `CloneModel()` first when a terrain chunk
needs its own independent bounds:

```c
chunkModel = CloneModel(baseModel);
// Assemble/replace that clone's terrain meshes, then assign accurate bounds.
SetModelRadius(chunkModel, chunkRadius);
PrintFlex(GetModelRadius(chunkModel));
```

`SetModelMeshRadius` follows the existing `GetModelMeshRadius` convention:
the mesh index addresses **GeoSet 0** only. Neither setter calculates radii
from vertices, adjusts animation, nor changes collision properties. The
new value is used on subsequent native rendering/culling operations.
The caller is responsible for a conservative radius; undersized bounds
may incorrectly hide visible geometry. Both verbs accept finite radii
of zero or greater and silently do nothing for an invalid model/mesh
handle. A rejected model write can also produce a warning in the log.

## Binary verification

User-supplied `JK.dll` (PE32 i386):
SHA-256 `d7fa73fa1e2fcb03fd1353033950691804c83ea902b4b26ba4099d9983916d14`.

In that binary (image base `0x10000000`):

- `GetModelRadius` handler at `0x100D2B40` calls native
  `sithCogExec_PopModel3` at `0x004E3140` and reads a `float`
  from `rdModel3+0x5C`.
- `GetModelMeshRadius` handler at `0x100D2B70` pops the mesh index
  before the model, checks `rdModel3+0x44` (GeoSet count), uses
  `rdModel3+0x24` (GeoSet 0 mesh count) and `+0x28` (mesh array),
  then reads a `float` at `meshes + mesh_index*0x70 + 0x54`.

The new handlers call the **same native model resolver**, including static
and `CloneModel()` IDs. Absolute DLL handler addresses are only documented
here; they are *not* hardcoded in the renderer. The only native entrypoint
used at runtime is `0x004E3140`, which belongs to the tested JKGM EXE,
not to OpenJKDF2. The existing tested-executable and memory-protection
checks guard the writes.

These setters require JkGRfxMod/renderer injection with a compatible JKGM
executable and JK.dll; they are not present in the unmodified community DLL.
