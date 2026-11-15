# JK.dll mesh mutation research (2026-11-14)

## Binary examined

User-supplied `JK.dll`, 786,432 bytes; PE32 x86, preferred image base
`0x10000000`; SHA-256
`d7fa73fa1e2fcb03fd1353033950691804c83ea902b4b26ba4099d9983916d14`.
The offsets below are **preferred virtual addresses in this exact file**, not
universal function addresses for other builds. Static analysis only; no
in-process test was performed.

## Registration: the key performance bug

The DLL's `InitNewCogVerbs` registration sequence includes:

| COG verb | Registration site | Handler |
| --- | --- | --- |
| `SetModelMesh` | `0x1005378B` | `0x1004E8A0` |
| `CloneModel` | `0x1005376E` | `0x1004E870` |
| `ChangeModelMaterial` | `0x10053751` | `0x1004E7B0` |
| `SetThingMesh` | `0x10053BF6` | `0x10050BC0` |

Only the first **three** are in the original 16-verb tracker in
`renderer/native_mesh_revision.cpp`; the fourth is not tracked. The
13 other verb names from that tracker are not directly registered by
**this DLL's** initializer. Their textual presence in read-only data is
not proof of registration. Other modules may register additional COG
verbs; inspecting this DLL alone cannot rule them out.

At the time of this analysis, `finish()` requires all 16 registrations,
so `mesh_revision::active()` is false and the GPU cache hashes its source
data every frame. Do **not** treat missing registrations as harmless until
other registration paths are identified. The former automatic 32-frame
audit has separately been made opt-in and defaults to disabled.

## Handler behavior

- `CloneModel` (`0x1004E870`): calls native `PopModel3` at
  `0x004E3140`, then calls the DLL cloning routine at `0x10087020`,
  returning its new model identifier. `0x10087020` appends an
  independent model to the global model collection and deep-copies mesh
  arrays using per-model allocations. It does not appear to mutate the
  original model's existing mesh contents. A new clone therefore should
  not force source-geometry validation of unrelated live meshes;
  **reused native pointer addresses still require cache lifecycle handling**.
- `SetModelMesh` (`0x1004E8A0`): resolves source and destination
  mesh selectors and two models; reads model mesh counts at `+0x24`,
  mesh array pointers at `+0x28`, and geoset counts at `+0x44`.
  The submesh stride observed in the copy loop is `0x70`. For the
  selected destination slot the handler copies mesh fields and deep-copies
  dynamically allocated arrays (helper `0x10050DA8`); the loop traverses
  model geosets. One call may affect the corresponding slot **in multiple
  geosets**, not one isolated `rdMesh*`.
- `ChangeModelMaterial` (`0x1004E7B0`): changes material references
  inside a model, including existing mesh-face references. Geometry
  rebuild requirements should be separated from material-binding changes.
- `SetThingMesh` (`0x10050BC0`): resolves a `SithThing` and its model,
  with a clone-on-write path using `0x10087020`, then updates the
  thing's target mesh. The *post-clone* model identity must be used
  for targeted cache invalidation.

## Recommended cache design

1. Intercept mutation calls that **actually execute**, across **all
   registration sources**, not merely `JK.dll::InitNewCogVerbs`.
2. Preserve COG arguments/results and never pop or replay arguments
   purely for diagnostics.
3. On `SetModelMesh`, record destination model identity and selected
   destination mesh slot; mark that slot in each affected geoset. On
   `SetThingMesh`, record the final thing-owned model after copy-on-write.
4. Track clone allocation/lifetime separately rather than invalidating
   unrelated cached meshes globally. Handle recycled `rdMesh*` addresses.
5. Model-wide invalidation is an acceptable temporary fallback **only for
   that model** when a verb's affected submeshes cannot be identified.
6. Keep the optional integrity audit at interval `0` by default. If
   enabled, report exact mismatches rather than permanently switching to
   full hashing; investigate untracked writers and add specific hooks.
7. Instrument the selected mutation pathway only once target identity
   has been verified in the supported game executable. Native game
   `PopModel3` is referenced by the DLL at `0x004E3140`, but this
   **does not establish a safe detour signature** for every executable.

Static disassembly is sufficient for the registration and handler
observations above; runtime validation is still required before replacing
cache invalidation semantics. No per-mesh invalidation implementation is
claimed by this document.
